#include "ui/KeybindsDialog.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <optional>
#include <string>

namespace playeverything {

namespace {

constexpr wchar_t kKeybindsClass[] = L"PlayEverythingKeybindsDlg";
constexpr wchar_t kCaptureClass[] = L"PlayEverythingKeyCaptureDlg";

constexpr int IDC_LIST_ACTIONS = 4001;
constexpr int IDC_LIST_BINDS = 4002;
constexpr int IDC_BTN_ADD = 4003;
constexpr int IDC_BTN_CHANGE = 4004;
constexpr int IDC_BTN_REMOVE = 4005;
constexpr int IDC_BTN_RESET = 4006;
constexpr int IDC_BTN_SAVE = 4007;
constexpr int IDC_BTN_CANCEL = 4008;
constexpr int IDC_CAPTURE_CANCEL = 4101;

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

HFONT CreateUiFont(UINT dpi, int ptSize = 9, int weight = FW_NORMAL) {
    return CreateFontW(
        -MulDiv(ptSize, static_cast<int>(dpi), 72),
        0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void CenterOverOwner(HWND hwnd, HWND owner, int w, int h) {
    RECT ownerRc{};
    if (owner && GetWindowRect(owner, &ownerRc)) {
        int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - w) / 2;
        int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - h) / 2;
        SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, nullptr, (sw - w) / 2, (sh - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void EnableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
}

struct CaptureState {
    bool done = false;
    bool captured = false;
    KeyCombo result{};
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT boldFont = nullptr;
};

LRESULT CALLBACK CaptureWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<CaptureState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
            if (LOWORD(wp) == IDC_CAPTURE_CANCEL && st) {
                st->done = true;
                st->captured = false;
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) {
                st->done = true;
                st->captured = false;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::optional<KeyCombo> CaptureKeyComboModal(HWND owner, HINSTANCE hInstance, const std::wstring& actionName) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &CaptureWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kCaptureClass;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForWindow(owner);
    if (dpi == 0) dpi = 96;

    CaptureState st{};
    st.font = CreateUiFont(dpi, 9, FW_NORMAL);
    st.boldFont = CreateUiFont(dpi, 10, FW_SEMIBOLD);

    int winW = ScalePx(380, dpi);
    int winH = ScalePx(168, dpi);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kCaptureClass,
        L"Press Key Combination",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, winW, winH,
        owner, nullptr, hInstance, &st);
    if (!hwnd) {
        if (st.font) DeleteObject(st.font);
        if (st.boldFont) DeleteObject(st.boldFont);
        return std::nullopt;
    }

    st.hwnd = hwnd;
    EnableDarkTitleBar(hwnd);
    CenterOverOwner(hwnd, owner, winW, winH);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    int pad = ScalePx(16, dpi);

    std::wstring line1 = L"Press any key combination for \"" + actionName + L"\"";
    HWND lbl1 = CreateWindowExW(
        0, L"STATIC", line1.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        pad, ScalePx(20, dpi), cw - pad * 2, ScalePx(24, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lbl1, WM_SETFONT, reinterpret_cast<WPARAM>(st.boldFont), TRUE);

    HWND lbl2 = CreateWindowExW(
        0, L"STATIC", L"Supports Ctrl, Alt, and Shift modifiers. Press Esc to cancel.",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        pad, ScalePx(50, dpi), cw - pad * 2, ScalePx(22, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lbl2, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    int btnW = ScalePx(92, dpi);
    int btnH = ScalePx(28, dpi);
    HWND btnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        (cw - btnW) / 2, ch - pad - btnH, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CAPTURE_CANCEL)), hInstance, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            UINT vk = static_cast<UINT>(msg.wParam);
            if (vk == VK_ESCAPE) {
                st.done = true;
                st.captured = false;
                continue;
            }
            if (!KeyCombo::IsModifierKey(vk)) {
                KeyCombo combo{};
                combo.vk = vk;
                combo.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                combo.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                combo.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (combo.IsValid()) {
                    st.result = combo;
                    st.captured = true;
                    st.done = true;
                    continue;
                }
            }
            continue;
        }
        if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    SetForegroundWindow(owner);

    if (st.font) DeleteObject(st.font);
    if (st.boldFont) DeleteObject(st.boldFont);

    if (st.captured) return st.result;
    return std::nullopt;
}

struct KeybindsDlgState {
    HINSTANCE hInstance = nullptr;
    HWND hwnd = nullptr;
    HWND listActions = nullptr;
    HWND listBinds = nullptr;
    HWND btnAdd = nullptr;
    HWND btnChange = nullptr;
    HWND btnRemove = nullptr;
    HFONT font = nullptr;
    HFONT boldFont = nullptr;
    UINT dpi = 96;

    KeybindManager workingCopy;
    PlayerAction selectedAction = PlayerAction::PlayPause;
    bool done = false;
    bool saved = false;

    void RefreshActionList() {
        const auto& actions = AllActions();
        for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
            std::wstring summary = workingCopy.GetBindingsSummary(actions[i]);
            ListView_SetItemText(listActions, i, 1, const_cast<LPWSTR>(summary.c_str()));
        }
    }

    void RefreshBindsList() {
        SendMessageW(listBinds, LB_RESETCONTENT, 0, 0);
        const auto& list = workingCopy.GetBindings(selectedAction);
        for (const auto& combo : list) {
            std::wstring txt = FormatKeyCombo(combo);
            SendMessageW(listBinds, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(txt.c_str()));
        }
        if (!list.empty()) {
            SendMessageW(listBinds, LB_SETCURSEL, 0, 0);
        }
        UpdateBindButtons();
    }

    void UpdateBindButtons() {
        LRESULT sel = SendMessageW(listBinds, LB_GETCURSEL, 0, 0);
        BOOL hasSel = (sel != LB_ERR);
        EnableWindow(btnChange, hasSel);
        EnableWindow(btnRemove, hasSel);
    }

    bool ConfirmConflictAndClear(const KeyCombo& combo, PlayerAction targetAction) {
        auto existing = workingCopy.FindAction(combo);
        if (!existing.has_value()) return true;
        if (*existing == targetAction) {
            std::wstring msg = L"\"" + FormatKeyCombo(combo) + L"\" is already assigned to " +
                               ActionDisplayName(targetAction) + L".";
            MessageBoxW(hwnd, msg.c_str(), L"Keybind Already Assigned", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        std::wstring msg = L"\"" + FormatKeyCombo(combo) + L"\" is currently assigned to \"" +
                           ActionDisplayName(*existing) + L"\".\n\nReassign it to \"" +
                           ActionDisplayName(targetAction) + L"\"?";
        int ans = MessageBoxW(hwnd, msg.c_str(), L"Keybind Conflict", MB_YESNO | MB_ICONQUESTION);
        if (ans != IDYES) return false;
        workingCopy.RemoveComboEverywhere(combo);
        return true;
    }

    void OnAdd() {
        auto captured = CaptureKeyComboModal(hwnd, hInstance, ActionDisplayName(selectedAction));
        if (!captured.has_value()) return;
        if (!ConfirmConflictAndClear(*captured, selectedAction)) return;
        workingCopy.AddBinding(selectedAction, *captured);
        RefreshActionList();
        RefreshBindsList();
    }

    void OnChange() {
        LRESULT sel = SendMessageW(listBinds, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) return;
        size_t idx = static_cast<size_t>(sel);

        auto captured = CaptureKeyComboModal(hwnd, hInstance, ActionDisplayName(selectedAction));
        if (!captured.has_value()) return;

        const auto& currentList = workingCopy.GetBindings(selectedAction);
        if (idx < currentList.size() && currentList[idx] == *captured) {
            return;
        }

        if (!ConfirmConflictAndClear(*captured, selectedAction)) return;
        // Re-check index in case conflict check touched anything (it only touches other actions)
        if (idx < workingCopy.GetBindings(selectedAction).size()) {
            workingCopy.ReplaceBinding(selectedAction, idx, *captured);
        } else {
            workingCopy.AddBinding(selectedAction, *captured);
        }
        RefreshActionList();
        RefreshBindsList();
        SendMessageW(listBinds, LB_SETCURSEL, static_cast<WPARAM>(idx), 0);
        UpdateBindButtons();
    }

    void OnRemove() {
        LRESULT sel = SendMessageW(listBinds, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) return;
        workingCopy.RemoveBinding(selectedAction, static_cast<size_t>(sel));
        RefreshActionList();
        RefreshBindsList();
    }

    void OnResetDefaults() {
        int ans = MessageBoxW(
            hwnd,
            L"Reset all keybinds back to their default shortcuts?",
            L"Reset Defaults",
            MB_YESNO | MB_ICONQUESTION);
        if (ans != IDYES) return;
        workingCopy.ResetDefaults();
        RefreshActionList();
        RefreshBindsList();
    }
};

LRESULT CALLBACK KeybindsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<KeybindsDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm && nm->idFrom == IDC_LIST_ACTIONS && nm->code == LVN_ITEMCHANGED) {
                auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lp);
                if ((nmlv->uNewState & LVIS_SELECTED) && !(nmlv->uOldState & LVIS_SELECTED)) {
                    const auto& actions = AllActions();
                    if (nmlv->iItem >= 0 && nmlv->iItem < static_cast<int>(actions.size())) {
                        st->selectedAction = actions[static_cast<size_t>(nmlv->iItem)];
                        st->RefreshBindsList();
                    }
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            if (!st) break;
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_LIST_BINDS) {
                if (code == LBN_SELCHANGE) {
                    st->UpdateBindButtons();
                } else if (code == LBN_DBLCLK) {
                    st->OnChange();
                }
                return 0;
            }
            switch (id) {
                case IDC_BTN_ADD:    st->OnAdd(); return 0;
                case IDC_BTN_CHANGE: st->OnChange(); return 0;
                case IDC_BTN_REMOVE: st->OnRemove(); return 0;
                case IDC_BTN_RESET:  st->OnResetDefaults(); return 0;
                case IDC_BTN_SAVE:
                    st->saved = true;
                    st->done = true;
                    return 0;
                case IDC_BTN_CANCEL:
                    st->saved = false;
                    st->done = true;
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_CLOSE:
            if (st) {
                st->saved = false;
                st->done = true;
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool ShowKeybindsDialog(HWND owner, HINSTANCE hInstance, KeybindManager& keybinds) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &KeybindsWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kKeybindsClass;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForWindow(owner);
    if (dpi == 0) dpi = 96;

    KeybindsDlgState st{};
    st.hInstance = hInstance;
    st.dpi = dpi;
    st.workingCopy = keybinds;
    st.font = CreateUiFont(dpi, 9, FW_NORMAL);
    st.boldFont = CreateUiFont(dpi, 9, FW_SEMIBOLD);

    int winW = ScalePx(620, dpi);
    int winH = ScalePx(430, dpi);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kKeybindsClass,
        L"Keybinds — Play Everything",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, winW, winH,
        owner, nullptr, hInstance, &st);
    if (!hwnd) {
        if (st.font) DeleteObject(st.font);
        if (st.boldFont) DeleteObject(st.boldFont);
        return false;
    }

    st.hwnd = hwnd;
    EnableDarkTitleBar(hwnd);
    CenterOverOwner(hwnd, owner, winW, winH);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    int pad = ScalePx(14, dpi);
    int gap = ScalePx(12, dpi);

    HWND header = CreateWindowExW(
        0, L"STATIC",
        L"Select an action on the left to add, change, or remove its keyboard shortcuts.",
        WS_CHILD | WS_VISIBLE,
        pad, pad, cw - pad * 2, ScalePx(20, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    int bodyTop = pad + ScalePx(28, dpi);
    int footerH = ScalePx(30, dpi);
    int bodyH = ch - bodyTop - pad * 2 - footerH;

    int leftW = ScalePx(340, dpi);
    int rightX = pad + leftW + gap;
    int rightW = cw - rightX - pad;

    // Left ListView: Actions & Shortcuts
    st.listActions = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        pad, bodyTop, leftW, bodyH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_ACTIONS)), hInstance, nullptr);
    SendMessageW(st.listActions, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    ListView_SetExtendedListViewStyle(st.listActions, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<LPWSTR>(L"Action");
    col.cx = ScalePx(150, dpi);
    ListView_InsertColumn(st.listActions, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Shortcuts");
    col.cx = leftW - ScalePx(172, dpi);
    ListView_InsertColumn(st.listActions, 1, &col);

    const auto& actions = AllActions();
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        std::wstring name = ActionDisplayName(actions[static_cast<size_t>(i)]);
        std::wstring summary = st.workingCopy.GetBindingsSummary(actions[static_cast<size_t>(i)]);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<LPWSTR>(name.c_str());
        ListView_InsertItem(st.listActions, &item);
        ListView_SetItemText(st.listActions, i, 1, const_cast<LPWSTR>(summary.c_str()));
    }

    // Right pane: Assigned Keybinds listbox + Add/Change/Remove buttons
    HWND rightLbl = CreateWindowExW(
        0, L"STATIC", L"Assigned Keybinds:",
        WS_CHILD | WS_VISIBLE,
        rightX, bodyTop, rightW, ScalePx(20, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(rightLbl, WM_SETFONT, reinterpret_cast<WPARAM>(st.boldFont), TRUE);

    int btnH = ScalePx(28, dpi);
    int btnGap = ScalePx(8, dpi);
    int buttonsBlockH = btnH * 3 + btnGap * 2;
    int listTop = bodyTop + ScalePx(24, dpi);
    int listH = bodyH - ScalePx(24, dpi) - buttonsBlockH - ScalePx(10, dpi);

    st.listBinds = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        rightX, listTop, rightW, listH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_BINDS)), hInstance, nullptr);
    SendMessageW(st.listBinds, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    int bx = rightX;
    int by = listTop + listH + ScalePx(10, dpi);

    st.btnAdd = CreateWindowExW(
        0, L"BUTTON", L"Add Keybind...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, by, rightW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_ADD)), hInstance, nullptr);
    SendMessageW(st.btnAdd, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    by += btnH + btnGap;
    st.btnChange = CreateWindowExW(
        0, L"BUTTON", L"Change Keybind...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, by, rightW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CHANGE)), hInstance, nullptr);
    SendMessageW(st.btnChange, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    by += btnH + btnGap;
    st.btnRemove = CreateWindowExW(
        0, L"BUTTON", L"Remove Keybind",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        bx, by, rightW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_REMOVE)), hInstance, nullptr);
    SendMessageW(st.btnRemove, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    // Bottom footer buttons
    int footerY = ch - pad - footerH;
    HWND btnReset = CreateWindowExW(
        0, L"BUTTON", L"Reset Defaults",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        pad, footerY, ScalePx(120, dpi), footerH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_RESET)), hInstance, nullptr);
    SendMessageW(btnReset, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    int okW = ScalePx(92, dpi);
    HWND btnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cw - pad - okW, footerY, okW, footerH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CANCEL)), hInstance, nullptr);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    HWND btnSave = CreateWindowExW(
        0, L"BUTTON", L"Save",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        cw - pad - okW * 2 - ScalePx(8, dpi), footerY, okW, footerH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE)), hInstance, nullptr);
    SendMessageW(btnSave, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    // Select first action
    ListView_SetItemState(st.listActions, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    st.selectedAction = PlayerAction::PlayPause;
    st.RefreshBindsList();

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(st.listActions);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.saved = false;
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

    if (st.font) DeleteObject(st.font);
    if (st.boldFont) DeleteObject(st.boldFont);

    if (st.saved) {
        keybinds = st.workingCopy;
        return true;
    }
    return false;
}

}  // namespace playeverything
