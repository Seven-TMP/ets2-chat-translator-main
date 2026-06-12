#include "translate_engine.h"
#include "text_codec.h"

#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <thread>

namespace
{
bool SplitUrl(const std::wstring& url, std::wstring& host, INTERNET_PORT& port, std::wstring& prefix, bool& tls)
{
    std::wstring u = url;
    tls = true;
    port = 443;
    if (u.rfind(L"https://", 0) == 0) {
        u = u.substr(8);
    } else if (u.rfind(L"http://", 0) == 0) {
        tls = false;
        port = 80;
        u = u.substr(7);
    }
    size_t slash = u.find(L'/');
    std::wstring hostPort = slash == std::wstring::npos ? u : u.substr(0, slash);
    prefix = slash == std::wstring::npos ? L"" : u.substr(slash);
    while (!prefix.empty() && prefix.back() == L'/') prefix.pop_back();

    size_t colon = hostPort.rfind(L':');
    if (colon != std::wstring::npos) {
        host = hostPort.substr(0, colon);
        try { port = (INTERNET_PORT)std::stoi(hostPort.substr(colon + 1)); } catch (...) { return false; }
    } else {
        host = hostPort;
    }
    return !host.empty();
}

bool IsChineseChar(wchar_t ch)
{
    return (ch >= 0x4E00 && ch <= 0x9FFF)
        || (ch >= 0x3400 && ch <= 0x4DBF)
        || (ch >= 0x3000 && ch <= 0x303F)
        || (ch >= 0xFF00 && ch <= 0xFFEF);
}

bool HasChinese(const std::wstring& value)
{
    for (wchar_t ch : value) {
        if (IsChineseChar(ch)) return true;
    }
    return false;
}

std::wstring LowerAscii(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return (wchar_t)towlower(ch);
    });
    return value;
}

std::wstring CompareKey(const std::wstring& value)
{
    std::wstring out;
    for (wchar_t ch : LowerAscii(value)) {
        if (iswalnum(ch)) out.push_back(ch);
    }
    return out;
}

bool LooksUntranslated(const std::wstring& input, const std::wstring& output, const RuntimeConfig& runtime)
{
    std::wstring out = text::Trim(output);
    if (out.empty()) return true;
    if (CompareKey(input) == CompareKey(out)) return true;
    if ((runtime.targetLanguage == L"zh-CN" || runtime.targetLanguage == L"zh" || runtime.targetLanguage == L"zh-Hans")
        && !HasChinese(out)) {
        // 如果输出没有中文但包含的文本和输入完全不同，可能是其他语言的意译
        // 仍判定为失败 — 目标就是中文
        return true;
    }
    return false;
}

bool EndsWithWord(const std::wstring& value, const std::wstring& suffix, std::wstring& prefix)
{
    if (value.size() < suffix.size()) return false;
    size_t start = value.size() - suffix.size();
    if (LowerAscii(value.substr(start)) != suffix) return false;
    if (start > 0 && !iswspace(value[start - 1])) return false;
    prefix = text::Trim(value.substr(0, start));
    return true;
}

bool IsChatEdgePunctuation(wchar_t ch)
{
    return ch == L'!' || ch == L'?' || ch == L'.' || ch == L',' || ch == L';' || ch == L':'
        || ch == L'\xFF01' || ch == L'\xFF1F' || ch == L'\x3002' || ch == L'\xFF0C'
        || ch == L'\xFF1B' || ch == L'\xFF1A';
}

std::wstring TrimChatEdgePunctuation(std::wstring value)
{
    value = text::Trim(value);
    while (!value.empty() && IsChatEdgePunctuation(value.front())) value.erase(value.begin());
    while (!value.empty() && IsChatEdgePunctuation(value.back())) value.pop_back();
    return text::Trim(value);
}

std::wstring TrimTrailingChatPunctuation(std::wstring value)
{
    value = text::Trim(value);
    while (!value.empty() && IsChatEdgePunctuation(value.back())) value.pop_back();
    return text::Trim(value);
}

std::wstring ShortPhraseFallback(const std::wstring& input)
{
    std::wstring lower = text::Trim(LowerAscii(input));
    if (lower.empty()) return L"";
    std::wstring edgeTrimmed = TrimChatEdgePunctuation(lower);
    std::wstring trailingTrimmed = TrimTrailingChatPunctuation(lower);

    struct Item { const wchar_t* key; const wchar_t* value; };
    static const Item exact[] = {
        { L"sry", L"抱歉" },
        { L"sr", L"抱歉" },
        { L"sorry", L"抱歉" },
        { L"pls", L"请" },
        { L"plz", L"请" },
        { L"please", L"请" },
        { L"no", L"不" },
        { L"yes", L"是" },
        { L"ok", L"好的" },
        { L"okay", L"好的" },
        { L"ty", L"谢谢" },
        { L"thx", L"谢谢" },
        { L"thanks", L"谢谢" },
        { L"thank you", L"谢谢" },
        { L"np", L"没事" },
        { L"nvm", L"没事" },
        { L"gg", L"打得好" },
        { L"wp", L"打得好" },
        { L"wait", L"等一下" },
        { L"stop", L"停一下" },
        { L"go", L"走" },
        { L"rec ban", L"已录屏，等封禁" },
        { L"rec", L"已录屏" },
        { L"recording", L"已录屏" },
        { L"hi", L"你好" },
        { L"hi!", L"你好" },
        { L"hello", L"你好" },
        { L"hey", L"嘿" },
        { L"yo", L"嘿" },
        { L"sup", L"咋样" },
        { L"o/", L"挥手" },
        { L"o//", L"挥手" },
        { L"\\o", L"挥手" },
        { L"\\o/", L"欢呼" },
        { L"bye", L"再见" },
        { L"cya", L"再见" },
        { L"cu", L"再见" },
        { L"gn", L"晚安" },
        { L"gn8", L"晚安" },
        { L"gm", L"早安" },
        { L"brb", L"马上回" },
        { L"afk", L"暂离" },
        { L"lol", L"哈哈" },
        { L"lmao", L"哈哈" },
        { L"xd", L"哈哈" },
        { L"wtf", L"什么鬼" },
        { L":)", L"微笑" },
        { L":(", L"难过" },
        { L":d", L"哈哈" },
        { L"<3", L"爱心" },
        { L"idk", L"我不知道" },
        { L"idc", L"无所谓" },
        { L"ikr", L"就是说" },
        { L"asap", L"尽快" },
        { L"bb", L"再见" },
        { L"k", L"好" },
        { L"kk", L"好" },
        { L"y", L"是" },
        { L"n", L"不" }
    };

    if (edgeTrimmed == L"sry pls" || edgeTrimmed == L"sorry pls" || edgeTrimmed == L"sry please" || edgeTrimmed == L"sorry please") {
        return L"抱歉，请";
    }

    if (lower.find(L"cannot connect to server") != std::wstring::npos ||
        lower.find(L"can't connect to server") != std::wstring::npos ||
        lower.find(L"can not connect to server") != std::wstring::npos) {
        return L"无法连接到服务器，可能是网络连接问题。";
    }
    if (lower.find(L"automatically reconnected") != std::wstring::npos ||
        lower.find(L"automaticly reconnected") != std::wstring::npos ||
        lower.find(L"reconnected within next") != std::wstring::npos ||
        lower.find(L"reconnect within next") != std::wstring::npos) {
        return L"将在接下来的几秒内自动重新连接。";
    }
    if (lower.find(L"connection established") != std::wstring::npos ||
        lower.find(L"connected to server") != std::wstring::npos) {
        return L"已连接到服务器。";
    }
    if (lower.find(L"connection refused") != std::wstring::npos ||
        lower.find(L"connection timed out") != std::wstring::npos) {
        return L"连接失败，请检查网络或稍后重试。";
    }

    for (const auto& item : exact) {
        if (lower == item.key || edgeTrimmed == item.key) return item.value;
    }

    std::wstring prefix;
    if (EndsWithWord(trailingTrimmed, L"sry pls", prefix) || EndsWithWord(trailingTrimmed, L"sorry pls", prefix)
        || EndsWithWord(trailingTrimmed, L"sry please", prefix) || EndsWithWord(trailingTrimmed, L"sorry please", prefix)) {
        size_t keep = input.size() - (lower.size() - prefix.size());
        return text::Trim(input.substr(0, keep)) + L" 抱歉，请";
    }

    for (const auto& item : exact) {
        if (EndsWithWord(trailingTrimmed, item.key, prefix)) {
            size_t keep = input.size() - (lower.size() - prefix.size());
            return text::Trim(input.substr(0, keep)) + L" " + item.value;
        }
    }

    return L"";
}

std::wstring JsonStringAfter(const std::string& json, const std::string& marker, const std::string& key)
{
    size_t p = json.find(marker);
    if (p == std::string::npos) return L"";
    return text::JsonString(json.substr(p), key);
}

std::wstring StatusLabel(const NetReply& reply)
{
    if (reply.status == 0) return L"status=0";
    return L"HTTP " + std::to_wstring(reply.status);
}

std::wstring PayloadPreview(const std::string& payload)
{
    std::wstring value = text::FromUtf8(payload);
    if (value.empty() && !payload.empty()) {
        value.assign(payload.begin(), payload.end());
    }
    value.erase(std::remove(value.begin(), value.end(), L'\r'), value.end());
    std::replace(value.begin(), value.end(), L'\n', L' ');
    std::replace(value.begin(), value.end(), L'\t', L' ');
    value = text::Trim(value);
    constexpr size_t kMaxPreview = 700;
    if (value.size() > kMaxPreview) value = value.substr(0, kMaxPreview) + L"...";
    return value;
}

std::wstring ReplyError(const NetReply& reply, const wchar_t* fallback)
{
    if (!reply.error.empty()) return reply.error;
    if (reply.status < 200 || reply.status >= 300) return StatusLabel(reply);
    return fallback;
}

std::wstring NowStamp()
{
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t buf[32] = {};
    swprintf_s(buf, L"%02u:%02u:%02u.%03u", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

std::wstring WidenAscii(const std::string& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (unsigned char ch : value) out.push_back((wchar_t)ch);
    return out;
}

std::wstring FirstJsonString(const std::string& json, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        std::wstring out = text::Trim(text::JsonString(json, key));
        if (!out.empty()) return out;
    }
    return L"";
}

std::wstring FirstJsonArrayString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return L"";
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return L"";
    p = json.find('[', p);
    if (p == std::string::npos) return L"";
    ++p;
    while (p < json.size() && (unsigned char)json[p] <= ' ') ++p;
    if (p >= json.size() || json[p] != '"') return L"";
    std::string probe = "{\"" + key + "\":" + json.substr(p) + "}";
    return text::JsonString(probe, key);
}

std::string FormPair(const char* key, const std::wstring& value)
{
    return std::string(key) + "=" + text::ToUtf8(text::PercentEncode(value));
}

std::wstring TargetForSimpleApi(const std::wstring& target)
{
    if (target == L"zh-CN" || target == L"zh-Hans") return L"zh";
    return target.empty() ? L"zh" : target;
}

bool ContainsAny(const std::wstring& text, const std::wstring& chars)
{
    return text.find_first_of(chars) != std::wstring::npos;
}

bool HasWordHint(const std::wstring& lower, std::initializer_list<const wchar_t*> words)
{
    std::wstring padded = L" " + lower + L" ";
    for (const wchar_t* word : words) {
        if (padded.find(L" " + std::wstring(word) + L" ") != std::wstring::npos) return true;
    }
    return false;
}

std::wstring GuessSourceLanguage(const std::wstring& input)
{
    std::wstring lower = LowerAscii(input);

    int cyrillic = 0;
    int greek = 0;
    int latin = 0;
    for (wchar_t ch : lower) {
        if (ch >= 0x0400 && ch <= 0x04FF) ++cyrillic;
        else if (ch >= 0x0370 && ch <= 0x03FF) ++greek;
        else if ((ch >= L'a' && ch <= L'z') || (ch >= 0x00C0 && ch <= 0x024F)) ++latin;
    }

    if (cyrillic > 0 && cyrillic >= latin) {
        if (ContainsAny(lower, L"іїєґ")) return L"uk";
        if (ContainsAny(lower, L"ћђљњџ")) return L"sr";
        if (ContainsAny(lower, L"ъ")) return L"bg";
        return L"ru";
    }
    if (greek > 0 && greek >= latin) return L"el";

    if (ContainsAny(lower, L"ığüşöçİĞÜŞÖÇ")) return L"tr";
    if (ContainsAny(lower, L"ąęłńóśźż")) return L"pl";
    if (ContainsAny(lower, L"ěščřžýáíéďťňů")) return L"cs";
    if (ContainsAny(lower, L"ăâîșţț")) return L"ro";
    if (ContainsAny(lower, L"ñ¿¡")) return L"es";
    if (ContainsAny(lower, L"ãõ")) return L"pt";
    if (ContainsAny(lower, L"ß")) return L"de";

    if (HasWordHint(lower, { L"ben", L"sen", L"kanka", L"kardeşim", L"tamam", L"abi", L"gel", L"git", L"yavaş" })) return L"tr";
    if (HasWordHint(lower, { L"ich", L"nicht", L"und", L"der", L"die", L"das", L"bitte", L"danke" })) return L"de";
    if (HasWordHint(lower, { L"bonjour", L"merci", L"avec", L"pour", L"pas", L"vous", L"oui" })) return L"fr";
    if (HasWordHint(lower, { L"hola", L"gracias", L"pero", L"porque", L"para", L"amigo" })) return L"es";
    if (HasWordHint(lower, { L"ciao", L"grazie", L"perché", L"sono", L"andare" })) return L"it";
    if (HasWordHint(lower, { L"obrigado", L"porque", L"para", L"você" })) return L"pt";
    if (HasWordHint(lower, { L"kurwa", L"dzieki", L"dzięki", L"prosze", L"proszę" })) return L"pl";

    return L"en";
}

std::wstring SourceOrGuess(const std::wstring& configured, const std::wstring& input)
{
    if (!configured.empty() && configured != L"auto") return configured;
    return GuessSourceLanguage(input);
}

std::wstring TargetForDeepL(const std::wstring& target)
{
    if (target == L"zh-CN" || target == L"zh" || target == L"zh-Hans") return L"ZH-HANS";
    if (target == L"zh-TW" || target == L"zh-Hant") return L"ZH-HANT";
    std::wstring out = target.empty() ? L"ZH-HANS" : target;
    std::transform(out.begin(), out.end(), out.begin(), towupper);
    return out;
}

std::wstring ProviderBaseUrl(const ProviderSettings& settings, const wchar_t* fallback)
{
    return settings.baseUrl.empty() ? fallback : settings.baseUrl;
}

std::string HexHash(const std::string& data, ALG_ID alg)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return "";
    if (!CryptCreateHash(provider, alg, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        return "";
    }
    BOOL ok = CryptHashData(hash, (const BYTE*)data.data(), (DWORD)data.size(), 0);
    DWORD size = 0;
    DWORD sizeLen = sizeof(size);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHSIZE, (BYTE*)&size, &sizeLen, 0);
    std::vector<BYTE> bytes(size);
    if (ok && size > 0) ok = CryptGetHashParam(hash, HP_HASHVAL, bytes.data(), &size, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok) return "";

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (BYTE b : bytes) {
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

std::vector<BYTE> Sha256Bytes(const std::string& data)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return {};
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        return {};
    }
    BOOL ok = CryptHashData(hash, (const BYTE*)data.data(), (DWORD)data.size(), 0);
    DWORD size = 0;
    DWORD sizeLen = sizeof(size);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHSIZE, (BYTE*)&size, &sizeLen, 0);
    std::vector<BYTE> bytes(size);
    if (ok && size > 0) ok = CryptGetHashParam(hash, HP_HASHVAL, bytes.data(), &size, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return ok ? bytes : std::vector<BYTE>{};
}

std::vector<BYTE> HmacSha256Bytes(const std::vector<BYTE>& key, const std::string& data)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HCRYPTKEY cryptoKey = 0;
    struct Blob
    {
        BLOBHEADER header;
        DWORD keySize;
    };

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return {};

    std::vector<BYTE> blob(sizeof(Blob) + key.size());
    auto* b = reinterpret_cast<Blob*>(blob.data());
    b->header.bType = PLAINTEXTKEYBLOB;
    b->header.bVersion = CUR_BLOB_VERSION;
    b->header.reserved = 0;
    b->header.aiKeyAlg = CALG_RC2;
    b->keySize = (DWORD)key.size();
    if (!key.empty()) memcpy(blob.data() + sizeof(Blob), key.data(), key.size());

    BOOL ok = CryptImportKey(provider, blob.data(), (DWORD)blob.size(), 0, CRYPT_IPSEC_HMAC_KEY, &cryptoKey);
    if (ok) ok = CryptCreateHash(provider, CALG_HMAC, cryptoKey, 0, &hash);
    HMAC_INFO info{};
    info.HashAlgid = CALG_SHA_256;
    if (ok) ok = CryptSetHashParam(hash, HP_HMAC_INFO, (BYTE*)&info, 0);
    if (ok) ok = CryptHashData(hash, (const BYTE*)data.data(), (DWORD)data.size(), 0);
    DWORD size = 0;
    DWORD sizeLen = sizeof(size);
    if (ok) ok = CryptGetHashParam(hash, HP_HASHSIZE, (BYTE*)&size, &sizeLen, 0);
    std::vector<BYTE> bytes(size);
    if (ok && size > 0) ok = CryptGetHashParam(hash, HP_HASHVAL, bytes.data(), &size, 0);
    if (hash) CryptDestroyHash(hash);
    if (cryptoKey) CryptDestroyKey(cryptoKey);
    CryptReleaseContext(provider, 0);
    return ok ? bytes : std::vector<BYTE>{};
}

std::string BytesHex(const std::vector<BYTE>& bytes)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (BYTE b : bytes) {
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

std::string Md5Hex(const std::wstring& data)
{
    return HexHash(text::ToUtf8(data), CALG_MD5);
}

std::string Sha256Hex(const std::wstring& data)
{
    return HexHash(text::ToUtf8(data), CALG_SHA_256);
}

std::wstring RandomSalt()
{
    ULONGLONG tick = GetTickCount64();
    DWORD pid = GetCurrentProcessId();
    return std::to_wstring(tick) + L"." + std::to_wstring(pid);
}

std::wstring UnixSeconds()
{
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value = {};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    unsigned long long seconds = (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
    return std::to_wstring(seconds);
}

std::wstring YoudaoInputForSign(const std::wstring& input)
{
    if (input.size() <= 20) return input;
    return input.substr(0, 10) + std::to_wstring(input.size()) + input.substr(input.size() - 10);
}

std::wstring SourceForBaidu(const std::wstring& source)
{
    return source.empty() ? L"auto" : source;
}

std::wstring TargetForBaidu(const std::wstring& target)
{
    if (target == L"zh-CN" || target == L"zh-Hans") return L"zh";
    if (target == L"zh-TW" || target == L"zh-Hant") return L"cht";
    if (target == L"ja") return L"jp";
    return target.empty() ? L"zh" : target;
}

std::wstring TargetForYoudao(const std::wstring& target)
{
    if (target == L"zh-CN" || target == L"zh" || target == L"zh-Hans") return L"zh-CHS";
    if (target == L"zh-TW" || target == L"zh-Hant") return L"zh-CHT";
    return target.empty() ? L"zh-CHS" : target;
}

std::wstring TargetForTencent(const std::wstring& target)
{
    if (target == L"zh-CN" || target == L"zh-Hans") return L"zh";
    if (target == L"zh-TW" || target == L"zh-Hant") return L"zh-TW";
    return target.empty() ? L"zh" : target;
}

std::wstring SourceForTencent(const std::wstring& source)
{
    if (source.empty() || source == L"auto") return L"auto";
    if (source == L"zh-CN" || source == L"zh-Hans") return L"zh";
    if (source == L"zh-TW" || source == L"zh-Hant") return L"zh-TW";
    return source;
}

std::string DateUtcForTencent()
{
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char buf[16] = {};
    sprintf_s(buf, "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

class AndeerTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return true; }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig&, HttpAgent& http, std::wstring& error) override
    {
        std::wstring path = L"/API/fanyi2.php?msg=" + text::PercentEncode(input);
        NetReply r = http.Get(L"api.andeer.top", 443, path, true);
        if (r.payload.empty()) r = http.Get(L"api.andeer.top", 80, path, false);
        LogLine(L"[TranslateHTTP] Andeer " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }

        std::wstring out = FirstJsonString(r.payload, {
            "\xe7\xbf\xbb\xe8\xaf\x91\xe5\x90\x8e",
            "result",
            "translation",
            "translatedText",
            "text",
            "data",
            "message"
        });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class MyMemoryTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return true; }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring from = SourceOrGuess(settings_.sourceLanguage, input);
        std::wstring to = runtime.targetLanguage.empty() ? L"zh-CN" : runtime.targetLanguage;
        std::wstring path = L"/get?q=" + text::PercentEncode(input) + L"&langpair=" + from + L"|" + to;
        NetReply r = http.Get(L"api.mymemory.translated.net", 443, path, true);
        LogLine(L"[TranslateHTTP] MyMemory source=" + from + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "translatedText", "match", "translation" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class ChatCompletionsTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override
    {
        return !settings_.baseUrl.empty() && !settings_.model.empty();
    }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        if (!SplitUrl(settings_.baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring target = runtime.targetLanguage.empty() ? L"zh-CN" : runtime.targetLanguage;
        std::wstring prompt = L"You translate TruckersMP/ETS2 multiplayer chat into " + target +
            L". Output ONLY the translation, no quotes, no explanations, no original text. "
            L"Always produce target-language output even for very short or unusual inputs. "
            L"Source can be ANY language (English, Turkish, Russian, German, French, Polish, Spanish, etc.) — translate all of them. "
            L"Translate slang and chat shorthand: sry/sorry=抱歉, pls/plz/please=请, ty/thx/thanks=谢谢, "
            L"np=没事, gg=打得好, wp=打得好, brb=马上回, afk=暂离, lol/xd=哈哈, idk=我不知道, "
            L"o/ or o//=挥手(打招呼), \\o/=欢呼, hi/hello/hey=你好, bye/cya=再见, gn=晚安, gm=早安, rec/recording=已录屏, wtf=什么鬼. "
            L"Keep player names, game IDs (numbers), tags in [brackets], URLs and emoji unchanged. "
            L"If input is purely punctuation/emoji with no meaning, output a brief Chinese description like 表情. "
            L"Never echo the original text as the answer.";

        std::string body = "{";
        body += "\"model\":\"" + text::EscapeJson(settings_.model) + "\",";
        body += "\"temperature\":0,";
        body += "\"messages\":[";
        body += "{\"role\":\"system\",\"content\":\"" + text::EscapeJson(prompt) + "\"},";
        body += "{\"role\":\"user\",\"content\":\"" + text::EscapeJson(input) + "\"}";
        body += "]}";

        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/json" },
            { L"Accept", L"application/json" }
        };
        if (!settings_.apiKey.empty()) headers.push_back({ L"Authorization", L"Bearer " + settings_.apiKey });
        NetReply r = http.Post(host, port, prefix + L"/chat/completions", tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = text::Trim(JsonStringAfter(r.payload, "\"message\"", "content"));
        if (out.empty()) out = text::Trim(JsonStringAfter(r.payload, "\"choices\"", "text"));
        if (out.empty()) out = FirstJsonString(r.payload, {
            "content",
            "text",
            "output_text",
            "translation",
            "translatedText",
            "result",
            "answer"
        });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class AnthropicTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override
    {
        return !settings_.apiKey.empty() && !settings_.model.empty();
    }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://api.anthropic.com/v1");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring target = runtime.targetLanguage.empty() ? L"zh-CN" : runtime.targetLanguage;
        std::wstring prompt = L"You translate TruckersMP/ETS2 multiplayer chat into " + target +
            L". Output ONLY the translation, no quotes, no explanations, no original text. "
            L"Always produce target-language output even for very short, slang-heavy, or multilingual inputs. "
            L"Translate common TruckersMP shorthand: rec/recording=已录屏, rec ban=已录屏，等封禁, wtf=什么鬼. "
            L"Keep player names, game IDs, tags in [brackets], URLs and emoji unchanged. "
            L"Never echo the original text as the answer.";

        std::string body = "{";
        body += "\"model\":\"" + text::EscapeJson(settings_.model) + "\",";
        body += "\"max_tokens\":512,";
        body += "\"system\":\"" + text::EscapeJson(prompt) + "\",";
        body += "\"messages\":[{\"role\":\"user\",\"content\":\"" + text::EscapeJson(input) + "\"}]";
        body += "}";

        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/json" },
            { L"Accept", L"application/json" },
            { L"x-api-key", settings_.apiKey },
            { L"anthropic-version", L"2023-06-01" }
        };
        NetReply r = http.Post(host, port, prefix + L"/messages", tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }

        std::wstring out = FirstJsonString(r.payload, { "text", "content" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class DeepLTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://api-free.deepl.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::string body = FormPair("text", input) + "&" +
            FormPair("target_lang", TargetForDeepL(runtime.targetLanguage));
        if (!settings_.sourceLanguage.empty() && settings_.sourceLanguage != L"auto") {
            std::wstring source = settings_.sourceLanguage;
            std::transform(source.begin(), source.end(), source.begin(), towupper);
            body += "&" + FormPair("source_lang", source);
        }

        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/x-www-form-urlencoded" },
            { L"Accept", L"application/json" },
            { L"Authorization", L"DeepL-Auth-Key " + settings_.apiKey }
        };
        NetReply r = http.Post(host, port, prefix + L"/v2/translate", tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "text", "translation", "translatedText" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class GoogleCloudTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://translation.googleapis.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring path = prefix + L"/language/translate/v2?key=" + text::PercentEncode(settings_.apiKey) +
            L"&q=" + text::PercentEncode(input) +
            L"&target=" + text::PercentEncode(TargetForSimpleApi(runtime.targetLanguage)) +
            L"&format=text";
        if (!settings_.sourceLanguage.empty() && settings_.sourceLanguage != L"auto") {
            path += L"&source=" + text::PercentEncode(settings_.sourceLanguage);
        }

        NetReply r = http.Get(host, port, path, tls, { { L"Accept", L"application/json" } });
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "translatedText", "translation", "text" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class LibreTranslateTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.baseUrl.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        if (!SplitUrl(settings_.baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring source = settings_.sourceLanguage.empty() ? L"auto" : settings_.sourceLanguage;
        std::wstring target = TargetForSimpleApi(runtime.targetLanguage);
        std::string body = "{";
        body += "\"q\":\"" + text::EscapeJson(input) + "\",";
        body += "\"source\":\"" + text::EscapeJson(source) + "\",";
        body += "\"target\":\"" + text::EscapeJson(target) + "\",";
        body += "\"format\":\"text\"";
        if (!settings_.apiKey.empty()) body += ",\"api_key\":\"" + text::EscapeJson(settings_.apiKey) + "\"";
        body += "}";

        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/json" },
            { L"Accept", L"application/json" }
        };
        NetReply r = http.Post(host, port, prefix + L"/translate", tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "translatedText", "translation", "text" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class MicrosoftTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://api.cognitive.microsofttranslator.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring path = prefix + L"/translate?api-version=3.0&to=" +
            text::PercentEncode(TargetForSimpleApi(runtime.targetLanguage));
        if (!settings_.sourceLanguage.empty() && settings_.sourceLanguage != L"auto") {
            path += L"&from=" + text::PercentEncode(settings_.sourceLanguage);
        }

        std::string body = "[{\"Text\":\"" + text::EscapeJson(input) + "\"}]";
        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/json" },
            { L"Accept", L"application/json" },
            { L"Ocp-Apim-Subscription-Key", settings_.apiKey }
        };
        if (!settings_.apiSecret.empty()) headers.push_back({ L"Ocp-Apim-Subscription-Region", settings_.apiSecret });

        NetReply r = http.Post(host, port, path, tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "text", "translatedText", "translation" });
        if (out.empty()) error = L"cannot parse response";
        return out;
    }
};

class BaiduTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty() && !settings_.apiSecret.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://fanyi-api.baidu.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring salt = RandomSalt();
        std::wstring from = SourceForBaidu(SourceOrGuess(settings_.sourceLanguage, input));
        std::wstring to = TargetForBaidu(runtime.targetLanguage);
        std::string sign = Md5Hex(settings_.apiKey + input + salt + settings_.apiSecret);
        if (sign.empty()) {
            error = L"sign failed";
            return L"";
        }

        std::wstring path = prefix + L"/api/trans/vip/translate?q=" + text::PercentEncode(input) +
            L"&from=" + text::PercentEncode(from) +
            L"&to=" + text::PercentEncode(to) +
            L"&appid=" + text::PercentEncode(settings_.apiKey) +
            L"&salt=" + text::PercentEncode(salt) +
            L"&sign=" + text::FromUtf8(sign);

        NetReply r = http.Get(host, port, path, tls, { { L"Accept", L"application/json" } });
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "dst", "translatedText", "translation" });
        if (out.empty()) {
            std::wstring msg = FirstJsonString(r.payload, { "error_msg", "error_code" });
            error = msg.empty() ? L"cannot parse response" : msg;
        }
        return out;
    }
};

class YoudaoTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty() && !settings_.apiSecret.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://openapi.youdao.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }

        std::wstring salt = RandomSalt();
        std::wstring curtime = UnixSeconds();
        std::wstring from = settings_.sourceLanguage.empty() ? L"auto" : settings_.sourceLanguage;
        std::wstring to = TargetForYoudao(runtime.targetLanguage);
        std::string sign = Sha256Hex(settings_.apiKey + YoudaoInputForSign(input) + salt + curtime + settings_.apiSecret);
        if (sign.empty()) {
            error = L"sign failed";
            return L"";
        }

        std::string body = FormPair("q", input) + "&" +
            FormPair("from", from) + "&" +
            FormPair("to", to) + "&" +
            FormPair("appKey", settings_.apiKey) + "&" +
            FormPair("salt", salt) + "&" +
            FormPair("sign", text::FromUtf8(sign)) + "&" +
            FormPair("signType", L"v3") + "&" +
            FormPair("curtime", curtime);

        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/x-www-form-urlencoded" },
            { L"Accept", L"application/json" }
        };
        NetReply r = http.Post(host, port, prefix + L"/api", tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonArrayString(r.payload, "translation");
        if (out.empty()) out = FirstJsonString(r.payload, { "translation", "translatedText", "tgt" });
        if (out.empty()) {
            std::wstring code = FirstJsonString(r.payload, { "errorCode" });
            error = code.empty() ? L"cannot parse response" : L"errorCode=" + code;
        }
        return out;
    }
};

class TencentTranslator final : public TranslateProvider
{
public:
    using TranslateProvider::TranslateProvider;
    bool Ready() const override { return !settings_.apiKey.empty() && !settings_.apiSecret.empty(); }

    std::wstring Translate(const std::wstring& input, const RuntimeConfig& runtime, HttpAgent& http, std::wstring& error) override
    {
        std::wstring host, prefix;
        INTERNET_PORT port = 443;
        bool tls = true;
        std::wstring baseUrl = ProviderBaseUrl(settings_, L"https://tmt.tencentcloudapi.com");
        if (!SplitUrl(baseUrl, host, port, prefix, tls)) {
            error = L"bad base_url";
            return L"";
        }
        if (prefix.empty()) prefix = L"/";

        std::wstring source = SourceForTencent(settings_.sourceLanguage);
        std::wstring target = TargetForTencent(runtime.targetLanguage);
        std::string body = "{";
        body += "\"SourceText\":\"" + text::EscapeJson(input) + "\",";
        body += "\"Source\":\"" + text::EscapeJson(source) + "\",";
        body += "\"Target\":\"" + text::EscapeJson(target) + "\",";
        body += "\"ProjectId\":0";
        body += "}";

        std::string timestamp = text::ToUtf8(UnixSeconds());
        std::string date = DateUtcForTencent();
        std::string service = "tmt";
        std::string canonicalHeaders = "content-type:application/json\nhost:" + text::ToUtf8(host) + "\n";
        std::string signedHeaders = "content-type;host";
        std::string hashedPayload = Sha256Hex(text::FromUtf8(body));
        std::string canonicalRequest = "POST\n/\n\n" + canonicalHeaders + "\n" + signedHeaders + "\n" + hashedPayload;
        std::string credentialScope = date + "/" + service + "/tc3_request";
        std::string stringToSign = "TC3-HMAC-SHA256\n" + timestamp + "\n" + credentialScope + "\n" +
            BytesHex(Sha256Bytes(canonicalRequest));

        std::vector<BYTE> secret = std::vector<BYTE>{ 'T', 'C', '3' };
        std::string secretUtf8 = text::ToUtf8(settings_.apiSecret);
        secret.insert(secret.end(), secretUtf8.begin(), secretUtf8.end());
        std::vector<BYTE> dateKey = HmacSha256Bytes(secret, date);
        std::vector<BYTE> serviceKey = HmacSha256Bytes(dateKey, service);
        std::vector<BYTE> signingKey = HmacSha256Bytes(serviceKey, "tc3_request");
        std::string signature = BytesHex(HmacSha256Bytes(signingKey, stringToSign));
        if (signature.empty()) {
            error = L"sign failed";
            return L"";
        }

        std::wstring authorization = L"TC3-HMAC-SHA256 Credential=" + settings_.apiKey + L"/" +
            WidenAscii(credentialScope) + L", SignedHeaders=" + WidenAscii(signedHeaders) +
            L", Signature=" + WidenAscii(signature);
        std::vector<HeaderPair> headers{
            { L"Content-Type", L"application/json" },
            { L"Accept", L"application/json" },
            { L"Authorization", authorization },
            { L"Host", host },
            { L"X-TC-Action", L"TextTranslate" },
            { L"X-TC-Version", L"2018-03-21" },
            { L"X-TC-Timestamp", WidenAscii(timestamp) },
            { L"X-TC-Region", settings_.model.empty() ? L"ap-guangzhou" : settings_.model }
        };

        NetReply r = http.Post(host, port, prefix, tls, body, headers);
        LogLine(L"[TranslateHTTP] " + Name() + L" " + StatusLabel(r) + L" payload=" + PayloadPreview(r.payload));
        if (r.payload.empty()) {
            error = ReplyError(r, L"empty reply");
            return L"";
        }
        if (r.status < 200 || r.status >= 300) {
            error = ReplyError(r, L"HTTP error");
            return L"";
        }
        std::wstring out = FirstJsonString(r.payload, { "TargetText", "translatedText", "translation", "text" });
        if (out.empty()) {
            std::wstring msg = FirstJsonString(r.payload, { "Message", "Code" });
            error = msg.empty() ? L"cannot parse response" : msg;
        }
        return out;
    }
};

std::unique_ptr<TranslateProvider> MakeProvider(const ProviderSettings& p)
{
    std::wstring kind = p.kind;
    std::transform(kind.begin(), kind.end(), kind.begin(), towlower);
    if (!p.enabled) return nullptr;
    if (kind == L"andeer") return std::make_unique<AndeerTranslator>(p);
    if (kind == L"mymemory") return std::make_unique<MyMemoryTranslator>(p);
    if (kind == L"anthropic" || kind == L"claude" || kind == L"anthropic_messages") {
        return std::make_unique<AnthropicTranslator>(p);
    }
    if (kind == L"deepl") return std::make_unique<DeepLTranslator>(p);
    if (kind == L"google_cloud" || kind == L"google_translate") return std::make_unique<GoogleCloudTranslator>(p);
    if (kind == L"libretranslate" || kind == L"libre_translate") return std::make_unique<LibreTranslateTranslator>(p);
    if (kind == L"microsoft" || kind == L"azure_translator") return std::make_unique<MicrosoftTranslator>(p);
    if (kind == L"baidu" || kind == L"baidu_translate") return std::make_unique<BaiduTranslator>(p);
    if (kind == L"youdao") return std::make_unique<YoudaoTranslator>(p);
    if (kind == L"tencent" || kind == L"tencent_cloud" || kind == L"tencent_tmt") return std::make_unique<TencentTranslator>(p);
    if (kind == L"openai_compatible" || kind == L"openai" || kind == L"chat_completions") {
        return std::make_unique<ChatCompletionsTranslator>(p);
    }
    return nullptr;
}
}

TranslateEngine::TranslateEngine() = default;

TranslateEngine::~TranslateEngine()
{
    Stop();
}

bool TranslateEngine::Start(RuntimeConfig runtime, std::vector<ProviderSettings> providerSettings, Done done)
{
    if (running_) return true;

    runtime_ = runtime;
    done_ = std::move(done);
    providers_.clear();
    for (const auto& p : providerSettings) {
        auto provider = MakeProvider(p);
        if (provider && provider->Ready()) {
            provider->SetLogger([this](const std::wstring& line) { LogLine(line); });
            providers_.push_back(std::move(provider));
        }
    }
    if (providers_.empty()) {
        std::lock_guard<std::mutex> g(errorLock_);
        lastError_ = L"no usable translation provider";
        return false;
    }
    providerHealth_.assign(providers_.size(), ProviderHealth{});

    running_ = true;
    activeWorkers_ = (std::max)(1, (std::min)(32, runtime_.workerCount));
    for (int i = 0; i < activeWorkers_; ++i) workers_.emplace_back(&TranslateEngine::Worker, this);
    return true;
}

void TranslateEngine::Stop()
{
    running_ = false;
    {
        std::lock_guard<std::mutex> g(jobsLock_);
        while (!jobs_.empty()) jobs_.pop();
        inFlight_.clear();
    }
    jobsCv_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
    workers_.clear();
    activeWorkers_ = 0;
}

void TranslateEngine::Submit(unsigned int id, const std::wstring& value)
{
    if (!running_ || value.empty()) return;

    std::wstring quick = ShortPhraseFallback(value);
    if (!quick.empty()) {
        if (done_) done_(id, quick);
        return;
    }

    {
        std::lock_guard<std::mutex> g(cacheLock_);
        auto found = cache_.find(value);
        if (found != cache_.end()) {
            if (done_) done_(id, found->second);
            return;
        }
    }

    {
        std::lock_guard<std::mutex> g(jobsLock_);
        auto inFlight = inFlight_.find(value);
        if (inFlight != inFlight_.end()) {
            inFlight->second.push_back(id);
            return;
        }
        if (jobs_.size() >= runtime_.queueLimit) {
            std::lock_guard<std::mutex> e(errorLock_);
            lastError_ = L"translation queue is full";
            return;
        }
        inFlight_[value].push_back(id);
        jobs_.push({ id, value });
    }
    jobsCv_.notify_one();
}

int ProviderIntervalMs(const std::wstring& kind)
{
    std::wstring lower = LowerAscii(kind);
    if (lower == L"mymemory") return 1100;
    if (lower == L"andeer") return 650;
    if (lower == L"libretranslate" || lower == L"libre_translate") return 450;
    return 80;
}

bool RetryableProviderError(const std::wstring& error)
{
    return error.empty()
        || error.find(L"request failed") != std::wstring::npos
        || error.find(L"empty reply") != std::wstring::npos
        || error.find(L"connect failed") != std::wstring::npos
        || error.find(L"status=0") != std::wstring::npos
        || error.find(L"HTTP 408") != std::wstring::npos
        || error.find(L"HTTP 429") != std::wstring::npos
        || error.find(L"HTTP 500") != std::wstring::npos
        || error.find(L"HTTP 502") != std::wstring::npos
        || error.find(L"HTTP 503") != std::wstring::npos
        || error.find(L"HTTP 504") != std::wstring::npos;
}

std::wstring TranslateEngine::LastError() const
{
    std::lock_guard<std::mutex> g(errorLock_);
    return lastError_;
}

void TranslateEngine::Worker()
{
    HttpAgent http(runtime_.timeoutMs);
    while (running_) {
        Job job;
        {
            std::unique_lock<std::mutex> g(jobsLock_);
            jobsCv_.wait_for(g, std::chrono::milliseconds(200), [this] {
                return !jobs_.empty() || !running_;
            });
            if (!running_) break;
            if (jobs_.empty()) continue;
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        std::wstring out = RunProviders(job.text, http);
        std::vector<unsigned int> ids;
        {
            std::lock_guard<std::mutex> g(jobsLock_);
            auto found = inFlight_.find(job.text);
            if (found != inFlight_.end()) {
                ids = std::move(found->second);
                inFlight_.erase(found);
            } else {
                ids.push_back(job.id);
            }
        }
        if (!out.empty() && done_ && running_) {
            for (unsigned int id : ids) done_(id, out);
        }
    }
}

std::wstring TranslateEngine::RunProviders(const std::wstring& value, HttpAgent& http)
{
    std::wstring fallback = ShortPhraseFallback(value);
    if (!fallback.empty()) {
        LogLine(L"[Translate] \"" + value + L"\" -> 本地字典: " + fallback);
        return fallback;
    }

    {
        std::lock_guard<std::mutex> g(cacheLock_);
        auto found = cache_.find(value);
        if (found != cache_.end()) return found->second;
    }

    // 纯标点/符号/单字符 不值得翻译
    bool allPunct = true;
    for (wchar_t ch : value) {
        if (iswalnum(ch) && ch > 127) { allPunct = false; break; }
        if (iswalpha(ch)) { allPunct = false; break; }
    }
    if (allPunct) {
        LogLine(L"[Translate] \"" + value + L"\" -> 跳过(纯符号)");
        return L"";
    }

    std::wstring errors;
    for (size_t i = 0; i < providers_.size(); ++i) {
        auto& p = providers_[i];
        int providerIndex = (int)i + 1;
        auto now = std::chrono::steady_clock::now();
        if (ProviderCoolingDown(i, now)) {
            std::wstring error = L"cooling down after repeated failures";
            LogLine(L"[Translate] \"" + value + L"\" provider[" + std::to_wstring(providerIndex) + L"/" +
                std::to_wstring(providers_.size()) + L"] " + p->Name() + L" skipped: " + error);
            if (!errors.empty()) errors += L" | ";
            errors += p->Name() + L": " + error;
            continue;
        }

        std::wstring error;
        std::wstring out;
        std::wstring requestAt = NowStamp();
        auto totalStarted = std::chrono::steady_clock::now();
        int attemptCount = 0;
        for (int attempt = 0; attempt < 2; ++attempt) {
            ++attemptCount;
            error.clear();
            WaitProviderTurn(i);
            out = p->Translate(value, runtime_, http, error);
            bool ok = !LooksUntranslated(value, out, runtime_);
            if (ok || !RetryableProviderError(error)) break;
            if (attempt == 0 && running_) {
                LogLine(L"[Translate] \"" + value + L"\" provider[" + std::to_wstring(providerIndex) + L"/" +
                    std::to_wstring(providers_.size()) + L"] " + p->Name() + L" retry after " +
                    (error.empty() ? L"empty error" : error));
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
            }
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - totalStarted).count();
        LogLine(L"[Translate] \"" + value + L"\" provider[" + std::to_wstring(providerIndex) + L"/" +
            std::to_wstring(providers_.size()) + L"] " + p->Name() + L" -> " +
            (out.empty() ? L"(empty)" : out) +
            L" at=" + requestAt +
            L" ms=" + std::to_wstring(elapsed) +
            L" attempts=" + std::to_wstring(attemptCount) +
            (error.empty() ? L"" : L" err=" + error));

        if (!LooksUntranslated(value, out, runtime_)) {
            NoteProviderResult(i, true, L"");
            RememberCache(value, out);
            return out;
        }
        if (!out.empty() && error.empty()) error = L"returned original/non-target text";
        NoteProviderResult(i, false, error);
        if (!errors.empty()) errors += L" | ";
        errors += p->Name() + L": " + error;
    }

    {
        std::lock_guard<std::mutex> g(errorLock_);
        lastError_ = errors.empty() ? L"translation failed" : errors;
    }
    return errors.empty() ? L"" : L"[\x7FFB\x8BD1\x5931\x8D25: " + errors + L"]";
}

void TranslateEngine::RememberCache(const std::wstring& text, const std::wstring& translated)
{
    std::lock_guard<std::mutex> g(cacheLock_);
    if (cache_.size() >= runtime_.cacheLimit && !cache_.empty()) cache_.erase(cache_.begin());
    cache_[text] = translated;
}

void TranslateEngine::WaitProviderTurn(size_t index)
{
    int intervalMs = 80;
    if (index < providers_.size()) intervalMs = ProviderIntervalMs(providers_[index]->Kind());

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point waitUntil{};
        {
            std::lock_guard<std::mutex> g(providerHealthLock_);
            if (index >= providerHealth_.size()) return;
            auto& h = providerHealth_[index];
            if (h.nextAllowed <= now) {
                h.nextAllowed = now + std::chrono::milliseconds(intervalMs);
                return;
            }
            waitUntil = h.nextAllowed;
        }

        auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil - now);
        if (waitMs.count() <= 0) continue;
        std::this_thread::sleep_for((std::min)(waitMs, std::chrono::milliseconds(200)));
    }
}

bool TranslateEngine::ProviderCoolingDown(size_t index, std::chrono::steady_clock::time_point now) const
{
    std::lock_guard<std::mutex> g(providerHealthLock_);
    if (index >= providerHealth_.size()) return false;
    return providerHealth_[index].coolUntil > now;
}

void TranslateEngine::NoteProviderResult(size_t index, bool success, const std::wstring& error)
{
    std::lock_guard<std::mutex> g(providerHealthLock_);
    if (index >= providerHealth_.size()) return;
    auto& h = providerHealth_[index];
    if (success) {
        h.failures = 0;
        h.coolUntil = {};
        return;
    }

    ++h.failures;
    if (RetryableProviderError(error) && h.failures >= 2) {
        int seconds = (std::min)(45, 8 + h.failures * 6);
        h.coolUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    }
}

void TranslateEngine::LogLine(const std::wstring& line) const
{
    if (logger_) logger_(line);
}

bool TranslateEngine::ShouldTranslate(const std::wstring& text)
{
    return !text.empty() && !text::MostlyChinese(text);
}
