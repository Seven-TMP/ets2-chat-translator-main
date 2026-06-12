#pragma once

#include "core_types.h"

#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <windows.h>

class ChatPanel
{
public:
    ChatPanel();
    ~ChatPanel();

    bool Open(HINSTANCE instance, const RuntimeConfig& runtime);
    void Close();
    void MessageLoop();
    void ApplyRuntime(const RuntimeConfig& runtime);
    bool SetOverlayHotkey(const std::wstring& hotkey);

    unsigned int Push(ChatEntry entry);
    void PatchTranslation(unsigned int id, const std::wstring& text);
    void Status(const std::wstring& text);
    void ToggleVisible();
    HWND Window() const { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void Paint(HDC dc, RECT bounds);
    void LayoutMetrics(HDC dc);
    int EntryHeight(const ChatEntry& entry) const;
    void ScrollToEnd();
    int ContentHeight() const;
    int ContentHeightUnlocked() const;
    void ResizeScroll();
    void OnWheel(int delta);
    void OnClick(int x, int y);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT titleFont_ = nullptr;

    mutable std::mutex lock_;
    std::vector<ChatEntry> entries_;
    std::queue<ChatEntry> pendingEntries_;
    std::queue<std::pair<unsigned int, std::wstring>> pendingTranslations_;
    unsigned int nextId_ = 1;
    std::wstring status_;

    int topBand_ = 46;
    int statusBand_ = 32;
    int rowH_ = 28;
    int subRowH_ = 24;
    int fontSize_ = 18;
    int contentWidth_ = 420;
    int scroll_ = 0;
    DWORD uiThreadId_ = 0;
    bool follow_ = true;
    bool closing_ = false;
    int hotkeyId_ = 0x4554;
    bool hotkeyRegistered_ = false;
    std::wstring overlayHotkey_ = L"Ctrl+Shift+T";

    void DrainPending();
};
