#include "ui/PlaylistPanel.h"

#include <algorithm>
#include <string>

namespace playeverything {

namespace {

constexpr wchar_t kPlaylistPanelClass[] = L"PlayEverythingPlaylistPanel";

constexpr int IDC_PL_LIST = 5001;
constexpr int IDC_PL_ADD = 5002;
constexpr int IDC_PL_UP = 5003;
constexpr int IDC_PL_DOWN = 5004;
constexpr int IDC_PL_REMOVE = 5005;
constexpr int IDC_PL_CLEAR = 5006;
constexpr int IDC_PL_CLOSE = 5007;

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

}  // namespace

PlaylistPanel::~PlaylistPanel() {
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
    if (bgBrush_) DeleteObject(bgBrush_);
    if (listBrush_) DeleteObject(listBrush_);
}

bool PlaylistPanel::Create(HWND parent, HINSTANCE hInstance, PlaylistPanelCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    bgBrush_ = CreateSolidBrush(RGB(19, 22, 30));
    listBrush_ = CreateSolidBrush(RGB(22, 25, 34));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &PlaylistPanel::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = bgBrush_;
    wc.lpszClassName = kPlaylistPanelClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kPlaylistPanelClass, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 264, 400,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;

    lblTitle_ = CreateWindowExW(
        0, L"STATIC", L"Playlist (0)",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 24,
        hwnd_, nullptr, hInstance, nullptr);

    btnClose_ = CreateWindowExW(
        0, L"BUTTON", L"\u00D7",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 24, 24,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_CLOSE)), hInstance, nullptr);

    listBox_ = CreateWindowExW(
        0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 100, 200,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_LIST)), hInstance, nullptr);

    btnAdd_ = CreateWindowExW(
        0, L"BUTTON", L"+ Add",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 50, 26,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_ADD)), hInstance, nullptr);

    btnUp_ = CreateWindowExW(
        0, L"BUTTON", L"\u25B2",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 28, 26,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_UP)), hInstance, nullptr);

    btnDown_ = CreateWindowExW(
        0, L"BUTTON", L"\u25BC",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 28, 26,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_DOWN)), hInstance, nullptr);

    btnRemove_ = CreateWindowExW(
        0, L"BUTTON", L"Remove",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 56, 26,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_REMOVE)), hInstance, nullptr);

    btnClear_ = CreateWindowExW(
        0, L"BUTTON", L"Clear",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 48, 26,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PL_CLEAR)), hInstance, nullptr);

    RecreateFonts();
    Layout();
    return true;
}

int PlaylistPanel::PixelWidth() const {
    return ScalePx(kLogicalWidthDip, dpi_);
}

void PlaylistPanel::SetDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    RecreateFonts();
    Layout();
}

void PlaylistPanel::RecreateFonts() {
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);

    font_ = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi_), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    titleFont_ = CreateFontW(
        -MulDiv(10, static_cast<int>(dpi_), 72),
        0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    if (lblTitle_) SendMessageW(lblTitle_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    for (HWND h : {btnClose_, listBox_, btnAdd_, btnUp_, btnDown_, btnRemove_, btnClear_}) {
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
}

void PlaylistPanel::Layout() {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = std::max<int>(1, rc.right - rc.left);
    int h = std::max<int>(1, rc.bottom - rc.top);

    int pad = ScalePx(10, dpi_);
    int headerH = ScalePx(26, dpi_);
    int closeW = ScalePx(26, dpi_);
    int btnH = ScalePx(28, dpi_);
    int gap = ScalePx(4, dpi_);

    if (lblTitle_) {
        SetWindowPos(lblTitle_, nullptr, pad + 1, pad + 2, w - pad * 2 - closeW - gap, headerH, SWP_NOZORDER);
    }
    if (btnClose_) {
        SetWindowPos(btnClose_, nullptr, w - pad - closeW, pad, closeW, headerH, SWP_NOZORDER);
    }

    int listTop = pad + headerH + ScalePx(8, dpi_);
    int footerY = h - pad - btnH;
    int listH = std::max<int>(20, footerY - listTop - ScalePx(8, dpi_));

    if (listBox_) {
        SetWindowPos(listBox_, nullptr, pad + 1, listTop, w - pad * 2 - 1, listH, SWP_NOZORDER);
    }

    // Bottom toolbar buttons
    int availW = w - pad * 2 - 1;
    int wUp = ScalePx(28, dpi_);
    int wDown = ScalePx(28, dpi_);
    int wClear = ScalePx(46, dpi_);
    int wRemove = ScalePx(58, dpi_);
    int wAdd = std::max<int>(ScalePx(46, dpi_), availW - wUp - wDown - wRemove - wClear - gap * 4);

    int x = pad + 1;
    if (btnAdd_) SetWindowPos(btnAdd_, nullptr, x, footerY, wAdd, btnH, SWP_NOZORDER);
    x += wAdd + gap;
    if (btnUp_) SetWindowPos(btnUp_, nullptr, x, footerY, wUp, btnH, SWP_NOZORDER);
    x += wUp + gap;
    if (btnDown_) SetWindowPos(btnDown_, nullptr, x, footerY, wDown, btnH, SWP_NOZORDER);
    x += wDown + gap;
    if (btnRemove_) SetWindowPos(btnRemove_, nullptr, x, footerY, wRemove, btnH, SWP_NOZORDER);
    x += wRemove + gap;
    if (btnClear_) SetWindowPos(btnClear_, nullptr, x, footerY, wClear, btnH, SWP_NOZORDER);
}

int PlaylistPanel::SelectedIndex() const {
    if (!listBox_) return -1;
    LRESULT sel = SendMessageW(listBox_, LB_GETCURSEL, 0, 0);
    return (sel == LB_ERR) ? -1 : static_cast<int>(sel);
}

void PlaylistPanel::Refresh(const PlaylistManager& playlist) {
    if (!hwnd_ || !listBox_) return;

    int prevSel = SelectedIndex();
    SendMessageW(listBox_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(listBox_, LB_RESETCONTENT, 0, 0);

    const auto& items = playlist.Items();
    int cur = playlist.CurrentIndex();
    for (size_t i = 0; i < items.size(); ++i) {
        std::wstring label;
        if (static_cast<int>(i) == cur) {
            label = L"\u25B6 " + items[i].displayName;
        } else {
            label = std::to_wstring(i + 1) + L". " + items[i].displayName;
        }
        SendMessageW(listBox_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    if (!items.empty()) {
        int sel = (cur >= 0 && cur < static_cast<int>(items.size())) ? cur : std::clamp(prevSel, 0, static_cast<int>(items.size()) - 1);
        SendMessageW(listBox_, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    }

    SendMessageW(listBox_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(listBox_, nullptr, TRUE);

    if (lblTitle_) {
        std::wstring title = L"Playlist (" + std::to_wstring(items.size()) + L")";
        SetWindowTextW(lblTitle_, title.c_str());
    }
}

LRESULT CALLBACK PlaylistPanel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PlaylistPanel* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<PlaylistPanel*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<PlaylistPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PlaylistPanel::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            FillRect(hdc, &rc, bgBrush_);
            // Subtle 1px left border
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(38, 43, 56));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, 0, 0, nullptr);
            LineTo(hdc, 0, rc.bottom);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, RGB(242, 244, 248));
            SetBkColor(hdc, RGB(19, 22, 30));
            return reinterpret_cast<LRESULT>(bgBrush_);
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, RGB(232, 236, 244));
            SetBkColor(hdc, RGB(22, 25, 34));
            return reinterpret_cast<LRESULT>(listBrush_);
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_PL_LIST && code == LBN_DBLCLK) {
                int sel = SelectedIndex();
                if (sel >= 0 && callbacks_.onPlayIndex) {
                    callbacks_.onPlayIndex(static_cast<size_t>(sel));
                }
                SetFocus(GetParent(hwnd_));
                return 0;
            }
            switch (id) {
                case IDC_PL_ADD:
                    if (callbacks_.onAddFiles) callbacks_.onAddFiles();
                    SetFocus(GetParent(hwnd_));
                    return 0;
                case IDC_PL_UP: {
                    int sel = SelectedIndex();
                    if (sel > 0 && callbacks_.onMoveUp) {
                        callbacks_.onMoveUp(static_cast<size_t>(sel));
                        SendMessageW(listBox_, LB_SETCURSEL, static_cast<WPARAM>(sel - 1), 0);
                    }
                    SetFocus(GetParent(hwnd_));
                    return 0;
                }
                case IDC_PL_DOWN: {
                    int sel = SelectedIndex();
                    LRESULT count = SendMessageW(listBox_, LB_GETCOUNT, 0, 0);
                    if (sel >= 0 && sel + 1 < count && callbacks_.onMoveDown) {
                        callbacks_.onMoveDown(static_cast<size_t>(sel));
                        SendMessageW(listBox_, LB_SETCURSEL, static_cast<WPARAM>(sel + 1), 0);
                    }
                    SetFocus(GetParent(hwnd_));
                    return 0;
                }
                case IDC_PL_REMOVE: {
                    int sel = SelectedIndex();
                    if (sel >= 0 && callbacks_.onRemoveIndex) {
                        callbacks_.onRemoveIndex(static_cast<size_t>(sel));
                    }
                    SetFocus(GetParent(hwnd_));
                    return 0;
                }
                case IDC_PL_CLEAR:
                    if (callbacks_.onClear) callbacks_.onClear();
                    SetFocus(GetParent(hwnd_));
                    return 0;
                case IDC_PL_CLOSE:
                    if (callbacks_.onClose) callbacks_.onClose();
                    SetFocus(GetParent(hwnd_));
                    return 0;
                default:
                    break;
            }
            break;
        }

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace playeverything
