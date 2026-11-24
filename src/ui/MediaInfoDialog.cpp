#include "ui/MediaInfoDialog.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <string>

namespace playeverything {

namespace {

constexpr wchar_t kInfoDlgClass[] = L"PlayEverythingMediaInfoDlg";
constexpr int IDC_INFO_LIST = 4301;
constexpr int IDC_INFO_COPY = 4302;
constexpr int IDC_INFO_CLOSE = 4303;

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

struct InfoDlgState {
    HWND hwnd = nullptr;
    const std::vector<MediaInfoEntry>* entries = nullptr;
    bool done = false;

    void CopyAllToClipboard() const {
        if (!entries) return;
        std::wstring text;
        std::wstring lastSection;
        for (const auto& e : *entries) {
            if (e.section != lastSection) {
                if (!text.empty()) text += L"\r\n";
                text += L"[" + e.section + L"]\r\n";
                lastSection = e.section;
            }
            text += e.key + L": " + e.value + L"\r\n";
        }
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* dst = GlobalLock(hMem);
                if (dst) {
                    memcpy(dst, text.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                } else {
                    GlobalFree(hMem);
                }
            }
            CloseClipboard();
        }
    }
};

LRESULT CALLBACK InfoDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<InfoDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
                if (LOWORD(wp) == IDC_INFO_COPY) {
                    st->CopyAllToClipboard();
                    return 0;
                }
                if (LOWORD(wp) == IDC_INFO_CLOSE || LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK) {
                    st->done = true;
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void ShowMediaInfoDialog(HWND owner, HINSTANCE hInstance, const std::vector<MediaInfoEntry>& entries) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &InfoDlgProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kInfoDlgClass;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForWindow(owner);
    if (dpi == 0) dpi = 96;

    HFONT font = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    int winW = ScalePx(640, dpi);
    int winH = ScalePx(480, dpi);

    InfoDlgState st{};
    st.entries = &entries;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kInfoDlgClass,
        L"Media Info — Play Everything",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, winW, winH,
        owner, nullptr, hInstance, &st);
    if (!hwnd) {
        if (font) DeleteObject(font);
        return;
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
    int btnH = ScalePx(28, dpi);

    int listH = ch - pad * 3 - btnH;
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
        pad, pad, cw - pad * 2, listH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INFO_LIST)), hInstance, nullptr);
    SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    int listW = cw - pad * 2 - ScalePx(22, dpi);
    int col0W = ScalePx(86, dpi);
    int col1W = ScalePx(140, dpi);
    int col2W = std::max(ScalePx(180, dpi), listW - col0W - col1W);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(L"Section");
    col.cx = col0W;
    ListView_InsertColumn(list, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Property");
    col.cx = col1W;
    ListView_InsertColumn(list, 1, &col);

    col.pszText = const_cast<LPWSTR>(L"Value");
    col.cx = col2W;
    ListView_InsertColumn(list, 2, &col);

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(e.section.c_str());
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, i, 1, const_cast<LPWSTR>(e.key.c_str()));
        ListView_SetItemText(list, i, 2, const_cast<LPWSTR>(e.value.c_str()));
    }

    int footerY = ch - pad - btnH;
    HWND btnCopy = CreateWindowExW(
        0, L"BUTTON", L"Copy All",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        pad, footerY, ScalePx(100, dpi), btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INFO_COPY)), hInstance, nullptr);
    SendMessageW(btnCopy, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    int closeW = ScalePx(92, dpi);
    HWND btnClose = CreateWindowExW(
        0, L"BUTTON", L"Close",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        cw - pad - closeW, footerY, closeW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INFO_CLOSE)), hInstance, nullptr);
    SendMessageW(btnClose, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(list);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = true;
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
}

}  // namespace playeverything
