#include "chat_panel.h"
#include "text_codec.h"

#include <algorithm>
#include <cwctype>
#include <windowsx.h>

namespace
{
const wchar_t* kClass = L"ETS2TranslatorPanelV4Simple";
const COLORREF cBack = RGB(9, 12, 18);
const COLORREF cPanel = RGB(18, 23, 34);
const COLORREF cCard = RGB(24, 30, 44);
const COLORREF cCardAlt = RGB(28, 35, 50);
const COLORREF cLine = RGB(52, 60, 78);
const COLORREF cText = RGB(243, 244, 246);
const COLORREF cDim = RGB(145, 158, 180);
const COLORREF cName = RGB(16, 185, 129);
const COLORREF cTime = RGB(125, 140, 165);
const COLORREF cTrans = RGB(255, 215, 100);
const COLORREF cWarn = RGB(239, 68, 68);
const COLORREF cBlue = RGB(59, 130, 246);
const COLORREF cCyan = RGB(34, 211, 238);
constexpr int kTimeColumnW = 68;

void Fill(HDC dc, RECT r, COLORREF color)
{
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void DrawTextLine(HDC dc, HFONT font, COLORREF color, const std::wstring& text, RECT r, UINT flags)
{
    HFONT old = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

void RoundFill(HDC dc, RECT r, int radius, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void StrokeRound(HDC dc, RECT r, int radius, COLORREF color)
{
    HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, hollow);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

std::wstring CompactStatus(const std::wstring& status)
{
    if (status.empty()) return L"翻译引擎准备就绪";
    return status;
}

int ApproxTextLines(const std::wstring& text, int width, int fontSize, int maxLines)
{
    if (text.empty()) return 1;
    int avgCharW = (std::max)(7, fontSize / 2);
    int charsPerLine = (std::max)(12, width / avgCharW);
    int lines = (int)((text.size() + charsPerLine - 1) / charsPerLine);
    return (std::max)(1, (std::min)(maxLines, lines));
}

bool ParseHotkey(const std::wstring& hotkey, UINT& modifiers, UINT& vk)
{
    modifiers = 0;
    vk = 0;
    std::wstring token;
    std::vector<std::wstring> parts;
    for (wchar_t ch : hotkey) {
        if (ch == L'+' || ch == L' ' || ch == L'\t' || ch == L'-') {
            token = text::Trim(token);
            if (!token.empty()) parts.push_back(token);
            token.clear();
        } else {
            token.push_back((wchar_t)towupper(ch));
        }
    }
    token = text::Trim(token);
    if (!token.empty()) parts.push_back(token);

    for (const auto& part : parts) {
        if (part == L"CTRL" || part == L"CONTROL") modifiers |= MOD_CONTROL;
        else if (part == L"SHIFT") modifiers |= MOD_SHIFT;
        else if (part == L"ALT") modifiers |= MOD_ALT;
        else if (part == L"WIN" || part == L"META") modifiers |= MOD_WIN;
        else if (part.size() == 1 && part[0] >= L'A' && part[0] <= L'Z') vk = (UINT)part[0];
        else if (part.size() == 1 && part[0] >= L'0' && part[0] <= L'9') vk = (UINT)part[0];
        else if (part.size() >= 2 && part[0] == L'F') {
            try {
                int n = std::stoi(part.substr(1));
                if (n >= 1 && n <= 24) vk = VK_F1 + (UINT)n - 1;
            } catch (...) {
                return false;
            }
        } else if (part == L"INSERT" || part == L"INS") vk = VK_INSERT;
        else if (part == L"DELETE" || part == L"DEL") vk = VK_DELETE;
        else if (part == L"HOME") vk = VK_HOME;
        else if (part == L"END") vk = VK_END;
        else if (part == L"PAGEUP" || part == L"PGUP") vk = VK_PRIOR;
        else if (part == L"PAGEDOWN" || part == L"PGDN") vk = VK_NEXT;
        else if (part == L"UP") vk = VK_UP;
        else if (part == L"DOWN") vk = VK_DOWN;
        else if (part == L"LEFT") vk = VK_LEFT;
        else if (part == L"RIGHT") vk = VK_RIGHT;
        else if (part == L"SPACE") vk = VK_SPACE;
        else if (part == L"TAB") vk = VK_TAB;
        else if (part == L"ESC" || part == L"ESCAPE") vk = VK_ESCAPE;
        else return false;
    }

    return modifiers != 0 && vk != 0;
}
}

ChatPanel::ChatPanel()
{
}

ChatPanel::~ChatPanel()
{
    Close();
}

bool ChatPanel::Open(HINSTANCE instance, const RuntimeConfig& runtime)
{
    instance_ = instance;
    uiThreadId_ = GetCurrentThreadId();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ChatPanel::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    ApplyRuntime(runtime);

    int w = 600;
    int h = 540;
    int x = GetSystemMetrics(SM_CXSCREEN) - w - 24;
    int y = 72;

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClass, L"ETS2 Chat Translator", WS_POPUP,
        x, y, w, h, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    SetLayeredWindowAttributes(hwnd_, 0, 250, LWA_ALPHA);
    SetOverlayHotkey(runtime.overlayHotkey);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void ChatPanel::ApplyRuntime(const RuntimeConfig& runtime)
{
    int nextFontSize = (std::max)(12, (std::min)(28, runtime.fontSize));

    HFONT nextFont = CreateFontW(nextFontSize, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT nextSmallFont = CreateFontW((std::max)(11, nextFontSize - 2), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT nextTitleFont = CreateFontW(nextFontSize + 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    if (!nextFont || !nextSmallFont || !nextTitleFont) {
        if (nextFont) DeleteObject(nextFont);
        if (nextSmallFont) DeleteObject(nextSmallFont);
        if (nextTitleFont) DeleteObject(nextTitleFont);
        return;
    }

    if (font_) DeleteObject(font_);
    if (smallFont_) DeleteObject(smallFont_);
    if (titleFont_) DeleteObject(titleFont_);
    font_ = nextFont;
    smallFont_ = nextSmallFont;
    titleFont_ = nextTitleFont;

    fontSize_ = nextFontSize;
    rowH_ = fontSize_ + 12;
    subRowH_ = fontSize_ + 9;
    topBand_ = fontSize_ + 34;
    statusBand_ = fontSize_ + 18;

    SetOverlayHotkey(runtime.overlayHotkey);

    if (hwnd_) {
        ScrollToEnd();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ChatPanel::Close()
{
    if (hwnd_) {
        if (hotkeyRegistered_) {
            UnregisterHotKey(hwnd_, hotkeyId_);
            hotkeyRegistered_ = false;
        }
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_) DeleteObject(font_);
    if (smallFont_) DeleteObject(smallFont_);
    if (titleFont_) DeleteObject(titleFont_);
    font_ = smallFont_ = titleFont_ = nullptr;
}

void ChatPanel::MessageLoop()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool ChatPanel::SetOverlayHotkey(const std::wstring& hotkey)
{
    std::wstring value = text::Trim(hotkey.empty() ? L"Ctrl+Shift+T" : hotkey);
    {
        std::lock_guard<std::mutex> guard(lock_);
        overlayHotkey_ = value;
    }
    if (!hwnd_) return false;

    if (uiThreadId_ != 0 && GetCurrentThreadId() != uiThreadId_) {
        PostMessageW(hwnd_, WM_APP + 4, 0, 0);
        return true;
    }

    if (hotkeyRegistered_) {
        UnregisterHotKey(hwnd_, hotkeyId_);
        hotkeyRegistered_ = false;
    }

    UINT modifiers = 0;
    UINT vk = 0;
    if (!ParseHotkey(value, modifiers, vk)) return false;

    hotkeyRegistered_ = RegisterHotKey(hwnd_, hotkeyId_, modifiers | MOD_NOREPEAT, vk) != FALSE;
    return hotkeyRegistered_;
}

unsigned int ChatPanel::Push(ChatEntry entry)
{
    unsigned int id;
    {
        std::lock_guard<std::mutex> guard(lock_);
        id = nextId_++;
        entry.id = id;
        pendingEntries_.push(std::move(entry));
    }

    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP + 3, 0, 0);
    }
    return id;
}

void ChatPanel::PatchTranslation(unsigned int id, const std::wstring& text)
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        pendingTranslations_.push({ id, text });
    }
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP + 3, 0, 0);
    }
}

void ChatPanel::Status(const std::wstring& text)
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        status_ = text;
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatPanel::ToggleVisible()
{
    if (!hwnd_) return;
    if (IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }

    ShowWindow(hwnd_, SW_SHOWNA);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK ChatPanel::WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ChatPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<ChatPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ChatPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_NCCREATE:
        return TRUE;

    case WM_CREATE:
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        self->Paint(mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (p.y >= rc.bottom - 12 && p.x >= rc.right - 12) return HTBOTTOMRIGHT;
        if (p.y >= rc.bottom - 8) return HTBOTTOM;
        if (p.x >= rc.right - 8) return HTRIGHT;
        if (p.y < self->topBand_ && p.x < rc.right - 40) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE: {
        self->ResizeScroll();
        int cx = LOWORD(lp);
        int cy = HIWORD(lp);
        HRGN rgn = CreateRoundRectRgn(0, 0, cx, cy, 16, 16);
        SetWindowRgn(hwnd, rgn, TRUE);
        return 0;
    }
    case WM_MOUSEWHEEL:
        self->OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_HOTKEY:
        if ((int)wp == self->hotkeyId_) self->ToggleVisible();
        return 0;
    case WM_LBUTTONDOWN:
        self->OnClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_APP + 1:
        self->ScrollToEnd();
        return 0;
    case WM_APP + 3:
        self->DrainPending();
        return 0;
    case WM_APP + 4: {
        std::wstring hotkey;
        {
            std::lock_guard<std::mutex> guard(self->lock_);
            hotkey = self->overlayHotkey_;
        }
        self->SetOverlayHotkey(hotkey);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        self->closing_ = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (self->hotkeyRegistered_) {
            UnregisterHotKey(hwnd, self->hotkeyId_);
            self->hotkeyRegistered_ = false;
        }
        self->hwnd_ = nullptr;
        if (self->closing_) PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ChatPanel::DrainPending()
{
    {
        std::lock_guard<std::mutex> guard(lock_);
        while (!pendingEntries_.empty()) {
            entries_.push_back(std::move(pendingEntries_.front()));
            pendingEntries_.pop();
        }
        while (!pendingTranslations_.empty()) {
            auto item = std::move(pendingTranslations_.front());
            pendingTranslations_.pop();
            for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
                if (it->id == item.first) {
                    it->translated = item.second;
                    break;
                }
            }
        }
        if (entries_.size() > 700) entries_.erase(entries_.begin(), entries_.begin() + 150);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (follow_) PostMessageW(hwnd_, WM_APP + 1, 0, 0);
}

void ChatPanel::Paint(HDC dc, RECT bounds)
{
    SetBkMode(dc, TRANSPARENT);
    Fill(dc, bounds, cBack);

    RECT outer{ 0, 0, bounds.right, bounds.bottom };
    RoundFill(dc, outer, 18, cPanel);
    StrokeRound(dc, { 0, 0, bounds.right - 1, bounds.bottom - 1 }, 18, RGB(45, 55, 75));

    RECT accent{ 16, 12, 20, topBand_ - 10 };
    RoundFill(dc, accent, 4, cBlue);

    RECT title{ 30, 0, bounds.right - 120, topBand_ };
    DrawTextLine(dc, titleFont_, cText, L"TruckersMP Chat", title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT tag{ bounds.right - 118, 14, bounds.right - 52, topBand_ - 12 };
    RoundFill(dc, tag, 12, RGB(22, 42, 62));
    DrawTextLine(dc, smallFont_, cCyan, L"LIVE", tag, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT close{ bounds.right - 42, 10, bounds.right - 14, topBand_ - 10 };
    RoundFill(dc, close, 8, RGB(35, 42, 56));
    DrawTextLine(dc, titleFont_, RGB(175, 185, 200), L"\x2715", close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::wstring status;
    {
        std::lock_guard<std::mutex> guard(lock_);
        status = status_;
    }
    RECT statusBox{ 16, topBand_, bounds.right - 16, topBand_ + statusBand_ - 6 };
    RoundFill(dc, statusBox, 10, RGB(13, 18, 27));
    RECT dot{ statusBox.left + 12, statusBox.top + 10, statusBox.left + 20, statusBox.top + 18 };
    RoundFill(dc, dot, 8, RGB(16, 185, 129));
    RECT statusText{ statusBox.left + 28, statusBox.top, statusBox.right - 12, statusBox.bottom };
    DrawTextLine(dc, smallFont_, cDim, CompactStatus(status), statusText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT area{ 0, topBand_ + statusBand_ + 2, bounds.right, bounds.bottom - 10 };
    HRGN clip = CreateRectRgn(area.left, area.top, area.right, area.bottom);
    SelectClipRgn(dc, clip);

    int y = area.top + 8 - scroll_;
    int left = 14;
    int right = bounds.right - 18;
    contentWidth_ = right - left - 24;
    {
        std::lock_guard<std::mutex> guard(lock_);
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            int h = EntryHeight(e);
            if (y > bounds.bottom) break;
            if (y + h >= area.top) {
                if (e.serviceLine) {
                    RECT card{ left, y + 2, right, y + h - 4 };
                    RoundFill(dc, card, 10, RGB(44, 26, 32));
                    RECT r{ card.left + 12, card.top, card.right - 12, card.bottom };
                    DrawTextLine(dc, smallFont_, cWarn, L"[" + e.time + L"] " + e.body, r,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                } else {
                    RECT card{ left, y + 2, right, y + h - 4 };
                    RoundFill(dc, card, 10, (i % 2 == 0) ? cCard : cCardAlt);
                    StrokeRound(dc, card, 10, RGB(39, 48, 65));

                    RECT timeRc{ card.left + 12, card.top + 10, card.left + 12 + kTimeColumnW, card.top + 10 + rowH_ };
                    DrawTextLine(dc, smallFont_, cTime, e.time, timeRc, DT_LEFT | DT_TOP | DT_SINGLELINE);

                    int contentX = card.left + 14 + kTimeColumnW;
                    int contentRight = card.right - 14;
                    int lineTop = card.top + 8;
                    if (!e.author.empty()) {
                        std::wstring name = e.author + L":";
                        RECT nameRc{ contentX, lineTop, contentRight, lineTop + rowH_ };
                        DrawTextLine(dc, font_, cName, name, nameRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
                        SIZE ns{};
                        HFONT old = (HFONT)SelectObject(dc, font_);
                        GetTextExtentPoint32W(dc, name.c_str(), (int)name.size(), &ns);
                        SelectObject(dc, old);
                        contentX += ns.cx + 6;
                        if (contentX > contentRight - 120) {
                            contentX = card.left + 14 + kTimeColumnW;
                            lineTop += rowH_ - 4;
                        }
                    }

                    int textLines = ApproxTextLines(e.body, contentRight - contentX, fontSize_, 3);
                    RECT msgRc{ contentX, lineTop, contentRight, lineTop + textLines * rowH_ };
                    DrawTextLine(dc, font_, cText, e.body, msgRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

                    if (!e.translated.empty()) {
                        int transTop = msgRc.bottom + 8;
                        int transLines = ApproxTextLines(e.translated, contentRight - (card.left + 14 + kTimeColumnW) - 22, fontSize_ - 2, 2);
                        RECT transBg{ card.left + 14 + kTimeColumnW, transTop, card.right - 12, transTop + transLines * subRowH_ + 8 };
                        RoundFill(dc, transBg, 8, RGB(38, 43, 54));

                        RECT transBar{ transBg.left, transBg.top + 4, transBg.left + 4, transBg.bottom - 4 };
                        RoundFill(dc, transBar, 4, cBlue);

                        RECT tr{ transBg.left + 12, transBg.top + 4, transBg.right - 10, transBg.bottom - 4 };
                        DrawTextLine(dc, smallFont_, cTrans, e.translated, tr,
                            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
                    }
                }
            }
            y += h;
        }
    }

    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);

    int content = ContentHeight();
    int view = area.bottom - area.top;
    if (content > view) {
        int trackTop = area.top + 8;
        int trackBottom = area.bottom - 8;
        RECT track{ bounds.right - 10, trackTop, bounds.right - 6, trackBottom };
        RoundFill(dc, track, 4, RGB(36, 44, 58));
        int thumbH = (std::max)(28, view * (trackBottom - trackTop) / content);
        int maxScroll = (std::max)(1, content - view);
        int thumbTop = trackTop + scroll_ * ((trackBottom - trackTop) - thumbH) / maxScroll;
        RECT thumb{ bounds.right - 10, thumbTop, bounds.right - 6, thumbTop + thumbH };
        RoundFill(dc, thumb, 4, RGB(92, 110, 140));
    }
}

int ChatPanel::EntryHeight(const ChatEntry& entry) const
{
    if (entry.serviceLine) return rowH_ + 12;
    int textWidth = (std::max)(80, contentWidth_ - kTimeColumnW - 10);
    int textLines = ApproxTextLines(entry.body, textWidth, fontSize_, 3);
    int h = 20 + textLines * rowH_ + 12;
    if (!entry.translated.empty()) {
        int transLines = ApproxTextLines(entry.translated, textWidth - 22, fontSize_ - 2, 2);
        h += transLines * subRowH_ + 16;
    }
    return (std::max)(rowH_ + 20, h);
}

int ChatPanel::ContentHeight() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return ContentHeightUnlocked();
}

int ChatPanel::ContentHeightUnlocked() const
{
    int total = 16;
    for (const auto& e : entries_) {
        total += EntryHeight(e);
    }
    return total;
}

void ChatPanel::ResizeScroll()
{
    // No native scrollbar in the simple overlay. Mouse wheel still scrolls.
}

void ChatPanel::ScrollToEnd()
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int view = rc.bottom - topBand_ - statusBand_ - 1;
    {
        std::lock_guard<std::mutex> guard(lock_);
        scroll_ = (std::max)(0, ContentHeightUnlocked() - view);
    }
    follow_ = true;
    ResizeScroll();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatPanel::OnWheel(int delta)
{
    scroll_ -= (delta / WHEEL_DELTA) * rowH_ * 3;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int view = rc.bottom - topBand_ - statusBand_ - 1;
    {
        std::lock_guard<std::mutex> guard(lock_);
        int content = ContentHeightUnlocked();
        int maxScroll = (std::max)(0, content - view);
        scroll_ = (std::max)(0, (std::min)(scroll_, maxScroll));
        follow_ = scroll_ >= maxScroll - rowH_;
    }
    ResizeScroll();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ChatPanel::OnClick(int x, int y)
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (y >= 6 && y <= topBand_ - 6 && x >= rc.right - 34 && x <= rc.right - 10) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}
