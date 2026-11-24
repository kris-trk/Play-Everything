#include "ui/CustomSpeedDialog.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace playeverything {

namespace {

constexpr wchar_t kSpeedDlgClass[] = L"PlayEverythingCustomSpeedDlg";
constexpr int IDC_SPEED_EDIT = 4201;
constexpr int IDC_SPEED_OK = 4202;
constexpr int IDC_SPEED_CANCEL = 4203;

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

struct SpeedDlgState {
    HWND hwnd = nullptr;
    HWND edit = nullptr;
    bool done = false;
    bool accepted = false;
    double value = 1.0;

    void TryAccept() {
        wchar_t buf[64]{};
        GetWindowTextW(edit, buf, static_cast<int>(std::size(buf)));
        std::wstring s(buf);
        // Accept German/international comma as decimal separator
        for (wchar_t& c : s) {
            if (c == L',') c = L'.';
        }
        try {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (!std::isfinite(v) || v < 0.1 || v > 16.0) {
                MessageBoxW(hwnd, L"Please enter a speed between 0.1 and 16.0 (e.g. 1.25 or 1.5).",
                            L"Invalid Speed", MB_OK | MB_ICONWARNING);
                return;
            }
            value = v;
            accepted = true;
            done = true;
        } catch (...) {
            MessageBoxW(hwnd, L"Please enter a valid number (e.g. 0.5, 1.25, 2.5).",
                        L"Invalid Speed", MB_OK | MB_ICONWARNING);
        }
    }
};

LRESULT CALLBACK SpeedDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SpeedDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            if (st) {
                if (LOWORD(wp) == IDC_SPEED_OK || LOWORD(wp) == IDOK) {
                    st->TryAccept();
                    return 0;
                }
                if (LOWORD(wp) == IDC_SPEED_CANCEL || LOWORD(wp) == IDCANCEL) {
                    st->done = true;
                    st->accepted = false;
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            if (st) {
                st->done = true;
                st->accepted = false;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

std::optional<double> ShowCustomSpeedDialog(HWND owner, HINSTANCE hInstance, double currentSpeed) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SpeedDlgProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kSpeedDlgClass;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForWindow(owner);
    if (dpi == 0) dpi = 96;

    HFONT font = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    int winW = ScalePx(320, dpi);
    int winH = ScalePx(160, dpi);

    SpeedDlgState st{};
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSpeedDlgClass,
        L"Custom Playback Speed",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, winW, winH,
        owner, nullptr, hInstance, &st);
    if (!hwnd) {
        if (font) DeleteObject(font);
        return std::nullopt;
    }
    st.hwnd = hwnd;

    BOOL dark = TRUE;
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

    RECT ownerRc{};
    if (owner && GetWindowRect(owner, &ownerRc)) {
        int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - winW) / 2;
        int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - winH) / 2;
        SetWindowPos(hwnd, nullptr, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    int pad = ScalePx(14, dpi);

    HWND lbl = CreateWindowExW(
        0, L"STATIC", L"Enter speed multiplier (0.1x to 16.0x):",
        WS_CHILD | WS_VISIBLE,
        pad, pad, cw - pad * 2, ScalePx(20, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    wchar_t curTxt[32]{};
    std::swprintf(curTxt, std::size(curTxt), L"%.2f", std::clamp(currentSpeed, 0.1, 16.0));
    st.edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", curTxt,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        pad, pad + ScalePx(26, dpi), cw - pad * 2, ScalePx(26, dpi),
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_EDIT)), hInstance, nullptr);
    SendMessageW(st.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    int btnW = ScalePx(84, dpi);
    int btnH = ScalePx(28, dpi);
    int btnY = ch - pad - btnH;

    HWND btnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cw - pad - btnW, btnY, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_CANCEL)), hInstance, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND btnOk = CreateWindowExW(
        0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        cw - pad - btnW * 2 - ScalePx(8, dpi), btnY, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SPEED_OK)), hInstance, nullptr);
    SendMessageW(btnOk, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = true;
            st.accepted = false;
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            st.TryAccept();
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);
    if (font) DeleteObject(font);

    if (st.accepted) return st.value;
    return std::nullopt;
}

}  // namespace playeverything
