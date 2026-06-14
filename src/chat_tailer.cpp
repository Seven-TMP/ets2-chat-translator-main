#include "chat_tailer.h"
#include "text_codec.h"

#include <sstream>

namespace
{
constexpr DWORD kActiveLogPollMs = 80;
}

ChatTailer::ChatTailer()
{
    wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

ChatTailer::~ChatTailer()
{
    Stop();
    if (wake_) CloseHandle(wake_);
}

void ChatTailer::Start(std::wstring logFolder, Sink sink)
{
    if (live_) return;
    folder_ = std::move(logFolder);
    sink_ = std::move(sink);
    files_.clear();
    if (wake_) ResetEvent(wake_);
    live_ = true;
    worker_ = std::thread(&ChatTailer::Run, this);
}

void ChatTailer::Stop()
{
    live_ = false;
    if (wake_) SetEvent(wake_);
    if (worker_.joinable()) worker_.join();
}

std::wstring ChatTailer::TodayFile() const
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[80] = {};
    swprintf_s(name, L"chat_%04u_%02u_%02u_log.txt", now.wYear, now.wMonth, now.wDay);
    return folder_ + L"\\" + name;
}

std::wstring ChatTailer::TodaySpawningFile() const
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96] = {};
    swprintf_s(name, L"log_spawning_%04u.%02u.%02u_log.txt", now.wYear, now.wMonth, now.wDay);
    return folder_ + L"\\" + name;
}

void ChatTailer::Run()
{
    while (live_) {
        std::wstring chatFile = TodayFile();
        std::wstring spawningFile = TodaySpawningFile();
        if (files_.size() < 2 || files_[0].path != chatFile || files_[1].path != spawningFile) {
            files_.clear();
            TailFile chat{};
            chat.path = chatFile;
            chat.offset.QuadPart = -1;
            TailFile spawning{};
            spawning.path = spawningFile;
            spawning.offset.QuadPart = 0;
            files_.push_back(std::move(chat));
            files_.push_back(std::move(spawning));
        }

        ReadTail(files_[0], false);
        ReadTail(files_[1], true);

        if (wake_ && WaitForSingleObject(wake_, kActiveLogPollMs) == WAIT_OBJECT_0) break;
    }
}

void ChatTailer::ReadTail(TailFile& file, bool spawning)
{
    HANDLE h = CreateFileW(file.path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size)) {
        // Chat 首次打开跳到文件末尾；spawning 从当天文件开头读，便于搜索当日临时编号。
        if (file.offset.QuadPart < 0) {
            file.offset.QuadPart = size.QuadPart;
        }

        if (size.QuadPart < file.offset.QuadPart) {
            file.offset.QuadPart = size.QuadPart;
            file.pendingText.clear();
        }

        if (size.QuadPart > file.offset.QuadPart) {
            LARGE_INTEGER seek = file.offset;
            SetFilePointerEx(h, seek, nullptr, FILE_BEGIN);

            LONGLONG want = size.QuadPart - file.offset.QuadPart;
            if (want > 1024 * 1024) want = 1024 * 1024;

            std::string bytes((size_t)want, '\0');
            DWORD got = 0;
            if (ReadFile(h, bytes.data(), (DWORD)want, &got, nullptr) && got > 0) {
                bytes.resize(got);
                EmitLines(file, text::DecodeLogBytes(bytes), spawning);
                file.offset.QuadPart += got;
            }
        }
    }

    CloseHandle(h);
}

void ChatTailer::EmitLines(TailFile& file, const std::wstring& textBlock, bool spawning)
{
    file.pendingText += textBlock;
    size_t start = 0;
    while (true) {
        size_t end = file.pendingText.find(L'\n', start);
        if (end == std::wstring::npos) break;
        std::wstring line = file.pendingText.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = text::Trim(line);
        if (!line.empty() && sink_) {
            ChatEntry entry = spawning ? ParseSpawningLine(line) : ParseChatLine(line);
            if (!entry.body.empty()) sink_(entry);
        }
        start = end + 1;
    }
    file.pendingText.erase(0, start);
}

ChatEntry ChatTailer::ParseChatLine(std::wstring line) const
{
    ChatEntry e;
    size_t pos = 0;

    if (!line.empty() && line[0] == L'[') {
        size_t end = line.find(L']', 1);
        if (end != std::wstring::npos) {
            e.channel = line.substr(1, end - 1);
            pos = end + 1;
        }
    } else if (line.rfind(L"VTC>", 0) == 0) {
        e.channel = L"VTC";
        pos = 4;
    }

    while (pos < line.size() && line[pos] == L' ') ++pos;
    if (pos < line.size() && line[pos] == L'[') {
        size_t end = line.find(L']', pos + 1);
        if (end != std::wstring::npos) {
            e.time = line.substr(pos + 1, end - pos - 1);
            pos = end + 1;
        }
    }

    while (pos < line.size() && line[pos] == L' ') ++pos;
    std::wstring rest = line.substr(pos);

    if (rest.rfind(L"[System]", 0) == 0 ||
        rest.rfind(L"[Job Tracker]", 0) == 0 ||
        rest.rfind(L"Connecting", 0) == 0 ||
        rest.rfind(L"Connection", 0) == 0) {
        e.serviceLine = true;
        e.body = rest;
        return e;
    }

    size_t cut = rest.find(L": ");
    if (cut != std::wstring::npos) {
        e.author = rest.substr(0, cut);
        e.body = rest.substr(cut + 2);
    } else {
        e.body = rest;
    }
    return e;
}

ChatEntry ChatTailer::ParseSpawningLine(std::wstring line) const
{
    ChatEntry e;
    e.infoLine = true;
    e.searchOnly = true;
    e.channel = L"SPAWN";

    size_t timeStart = line.find(L'[');
    size_t timeEnd = line.find(L']', timeStart == std::wstring::npos ? 0 : timeStart + 1);
    if (timeStart != std::wstring::npos && timeEnd != std::wstring::npos && timeEnd > timeStart) {
        e.time = line.substr(timeStart + 1, timeEnd - timeStart - 1);
    }

    size_t open = line.find(L'(');
    size_t marker = line.find(L" - TMPID:", open == std::wstring::npos ? 0 : open);
    if (open == std::wstring::npos || marker == std::wstring::npos || marker <= open + 1) return e;

    std::wstring nameAndTemp = text::Trim(line.substr(open + 1, marker - open - 1));
    size_t tempOpen = nameAndTemp.rfind(L'(');
    size_t tempClose = nameAndTemp.rfind(L')');
    std::wstring name = nameAndTemp;
    std::wstring tempId;
    if (tempOpen != std::wstring::npos && tempClose == nameAndTemp.size() - 1 && tempClose > tempOpen + 1) {
        name = text::Trim(nameAndTemp.substr(0, tempOpen));
        tempId = nameAndTemp.substr(tempOpen + 1, tempClose - tempOpen - 1);
    }

    auto valueAfter = [&](const std::wstring& key) {
        size_t p = line.find(key, marker);
        if (p == std::wstring::npos) return std::wstring();
        p += key.size();
        size_t end = line.find(L" - ", p);
        size_t close = line.find(L')', p);
        if (end == std::wstring::npos || (close != std::wstring::npos && close < end)) end = close;
        if (end == std::wstring::npos) end = line.size();
        return text::Trim(line.substr(p, end - p));
    };

    std::wstring tmpId = valueAfter(L"TMPID:");
    std::wstring steamId = valueAfter(L"SteamID64:");
    std::wstring tag = valueAfter(L"Tag:");

    std::wstring body = L"日志信息：";
    if (!name.empty()) body += name;
    if (!tempId.empty()) body += L"  临时编号 " + tempId;
    if (!tmpId.empty()) body += L"  TMPID " + tmpId;
    if (!steamId.empty()) body += L"  SteamID64 " + steamId;
    if (!tag.empty()) body += L"  Tag " + tag;
    e.author = name;
    e.body = body;
    e.translated = body;
    return e;
}
