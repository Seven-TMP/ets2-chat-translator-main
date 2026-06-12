#include "app_runtime.h"

#include "text_codec.h"
#include "win_paths.h"

#include <algorithm>
#include <cwctype>

AppRuntime::AppRuntime(HINSTANCE dll, scs_log_t logger, std::wstring gameId, std::wstring gameName)
    : dll_(dll)
    , logger_(logger)
    , gameId_(std::move(gameId))
    , gameName_(std::move(gameName))
{
}

AppRuntime::~AppRuntime()
{
    Stop();
}

bool AppRuntime::Start()
{
    if (alive_) return true;
    alive_ = true;
    ui_ = std::thread(&AppRuntime::UiThread, this);
    return true;
}

void AppRuntime::Stop()
{
    if (!alive_ && !ui_.joinable()) return;
    alive_ = false;

    if (tailer_) tailer_->Stop();
    if (translator_) translator_->Stop();
    if (panel_ && panel_->Window()) PostMessageW(panel_->Window(), WM_CLOSE, 0, 0);

    if (ui_.joinable()) ui_.join();
}

void AppRuntime::UiThread()
{
    if (!Boot()) {
        Teardown();
        alive_ = false;
        return;
    }

    panel_->MessageLoop();
    Teardown();
    alive_ = false;
}

bool AppRuntime::Boot()
{
    pluginFolder_ = paths::ModuleFolder(dll_);
    configFile_ = pluginFolder_ + L"\\ets2_chat_translator_config.json";
    std::wstring lowerGame = gameId_ + L" " + gameName_;
    std::transform(lowerGame.begin(), lowerGame.end(), lowerGame.begin(), [](wchar_t ch) {
        return (wchar_t)towlower(ch);
    });
    bool ats = lowerGame.find(L"ats") != std::wstring::npos
        || lowerGame.find(L"american") != std::wstring::npos;
    logFolder_ = paths::DocumentsFolder() + (ats ? L"\\ATSMP\\logs" : L"\\ETS2MP\\logs");

    if (!paths::ExistsFile(configFile_)) settings::WriteDefaultFile(configFile_);
    settings_ = settings::Load(configFile_);
    configWriteTime_ = ConfigWriteTime();

    panel_ = std::make_unique<ChatPanel>();
    if (!panel_->Open(dll_, settings_.runtime)) {
        Log("[ChatTranslator] failed to create panel");
        return false;
    }
    Log("[ChatTranslator] panel created");

    bool translationOk = StartTranslator();

    std::wstring status = L"Logs: " + logFolder_;
    if (translationOk) {
        status += L" | providers: " + std::to_wstring(translator_->ProviderCount());
        status += L" | workers: " + std::to_wstring(translator_->WorkerCount());
        Log("[ChatTranslator] translation engine started");
    } else {
        status += L" | translation disabled: " + translator_->LastError();
        LogValue(L"[ChatTranslator] translation disabled: ", translator_->LastError());
    }
    panel_->Status(status);

    tailer_ = std::make_unique<ChatTailer>();
    tailer_->Start(logFolder_, [this](const ChatEntry& entry) {
        AcceptChat(entry);
    });

    Log("[ChatTranslator] runtime started");
    LogValue(L"[ChatTranslator] game: ", gameName_.empty() ? gameId_ : gameName_ + L" (" + gameId_ + L")");
    LogValue(L"[ChatTranslator] log folder: ", logFolder_);
    return true;
}

void AppRuntime::Teardown()
{
    {
        std::lock_guard<std::mutex> g(translatorLock_);
        if (translator_) translator_->Stop();
    }
    if (tailer_) tailer_->Stop();
    {
        std::lock_guard<std::mutex> g(translatorLock_);
        translator_.reset();
    }
    tailer_.reset();
    panel_.reset();
}

void AppRuntime::AcceptChat(const ChatEntry& entry)
{
    if (!alive_ || !panel_) return;
    CheckConfigReload();
    unsigned int id = panel_->Push(entry);
    if (translator_ && !entry.serviceLine && TranslateEngine::ShouldTranslate(entry.body)) {
        std::lock_guard<std::mutex> g(translatorLock_);
        if (translator_) translator_->Submit(id, entry.body);
    }
}

void AppRuntime::CheckConfigReload()
{
    FILETIME current = ConfigWriteTime();
    if (CompareFileTime(&current, &configWriteTime_) == 0) return;

    configWriteTime_ = current;
    AppSettings reloaded = settings::Load(configFile_);
    settings_ = std::move(reloaded);
    if (panel_) panel_->ApplyRuntime(settings_.runtime);

    Log("[ChatTranslator] config changed, reloading translation engine");
    bool ok = StartTranslator();
    if (panel_) {
        std::wstring status = L"Logs: " + logFolder_;
        if (ok && translator_) {
            status += L" | providers: " + std::to_wstring(translator_->ProviderCount());
            status += L" | workers: " + std::to_wstring(translator_->WorkerCount());
            status += L" | config reloaded";
        } else if (translator_) {
            status += L" | translation disabled: " + translator_->LastError();
        } else {
            status += L" | translation disabled";
        }
        panel_->Status(status);
    }
}

bool AppRuntime::StartTranslator()
{
    auto next = std::make_unique<TranslateEngine>();
    next->SetLogger([this](const std::wstring& line) {
        LogValue(L"", line);
    });

    bool ok = next->Start(settings_.runtime, settings_.providers,
        [this](unsigned int id, const std::wstring& translated) {
            if (alive_ && panel_) panel_->PatchTranslation(id, translated);
        });

    std::lock_guard<std::mutex> g(translatorLock_);
    if (translator_) translator_->Stop();
    translator_ = std::move(next);

    if (ok) {
        Log("[ChatTranslator] translation engine started");
    } else if (translator_) {
        LogValue(L"[ChatTranslator] translation disabled: ", translator_->LastError());
    }
    return ok;
}

FILETIME AppRuntime::ConfigWriteTime() const
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(configFile_.c_str(), GetFileExInfoStandard, &data)) {
        return data.ftLastWriteTime;
    }
    FILETIME empty = {};
    return empty;
}

void AppRuntime::Log(const char* message) const
{
    if (logger_) logger_(SCS_LOG_TYPE_message, message);
}

void AppRuntime::LogValue(const std::wstring& prefix, const std::wstring& value) const
{
    if (!logger_) return;
    std::string msg = text::ToUtf8(prefix + value);
    logger_(SCS_LOG_TYPE_message, msg.c_str());
}
