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

    bool Open(HINSTANCE instance, const RuntimeConfig& runtime, const std::wstring& windowStatePath = L"");
    void Close();
    void MessageLoop();
    void ApplyRuntime(const RuntimeConfig& runtime);
    bool SetOverlayHotkey(const std::wstring& hotkey);

    unsigned int Push(ChatEntry entry);
    void PatchTranslation(unsigned int id, const std::wstring& text);
    void Status(const std::wstring& text);
    void ToggleVisible();
    HWND Window() const { return hwnd_; }
    bool IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_) != FALSE; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void Paint(HDC dc, RECT bounds);
    void RenderLayered();
    void LayoutSearchBox(RECT bounds);
    void UpdateSearchText();
    bool EntryMatches(const ChatEntry& entry) const;
    int MatchCountUnlocked() const;
    void UpdateContentWidth(int clientWidth);
    int EntryHeight(HDC dc, const ChatEntry& entry) const;
    void ScrollToEnd();
    int ContentHeight(HDC dc) const;
    int ContentHeightUnlocked(HDC dc) const;
    void ResizeScroll();
    void OnWheel(int delta);
    void OnClick(int x, int y);
    void SaveWindowState() const;

    HWND hwnd_ = nullptr;
    HWND searchBox_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;

    mutable std::mutex lock_;
    std::vector<ChatEntry> entries_;
    std::queue<ChatEntry> pendingEntries_;
    std::queue<std::pair<unsigned int, std::wstring>> pendingTranslations_;
    unsigned int nextId_ = 1;
    std::wstring status_;
    std::wstring searchText_;
    std::wstring searchDisplayText_;

    int topBand_ = 46;
    int statusBand_ = 32;
    int rowH_ = 28;
    int subRowH_ = 24;
    int fontSize_ = 18;
    int overlayOpacity_ = 98;
    int contentWidth_ = 420;
    int scroll_ = 0;
    DWORD uiThreadId_ = 0;
    bool follow_ = true;
    bool closing_ = false;
    int hotkeyId_ = 0x4554;
    int searchBoxId_ = 0x4555;
    bool hotkeyRegistered_ = false;
    std::wstring overlayHotkey_ = L"Ctrl+Shift+T";
    std::wstring windowStatePath_;
    RECT searchBoxRect_{};

    void DrainPending();
};
