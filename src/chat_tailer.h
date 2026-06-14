#pragma once

#include "core_types.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

class ChatTailer
{
public:
    using Sink = std::function<void(const ChatEntry&)>;

    ChatTailer();
    ~ChatTailer();

    void Start(std::wstring logFolder, Sink sink);
    void Stop();

private:
    struct TailFile
    {
        std::wstring path;
        LARGE_INTEGER offset{};
        std::wstring pendingText;
    };

    void Run();
    std::wstring TodayFile() const;
    std::wstring TodaySpawningFile() const;
    void ReadTail(TailFile& file, bool spawning);
    ChatEntry ParseChatLine(std::wstring line) const;
    ChatEntry ParseSpawningLine(std::wstring line) const;
    void EmitLines(TailFile& file, const std::wstring& text, bool spawning);

    std::wstring folder_;
    std::vector<TailFile> files_;
    Sink sink_;
    std::thread worker_;
    std::atomic<bool> live_{ false };
    HANDLE wake_ = nullptr;
};
