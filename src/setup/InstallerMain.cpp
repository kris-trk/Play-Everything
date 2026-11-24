#include "setup/WindowsIntegration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kSetupClass[] = L"PlayEverythingSetupWindow";
constexpr int IDI_SETUP_ICON = 101;

constexpr int IDC_EDIT_DIR = 6001;
constexpr int IDC_BTN_BROWSE = 6002;
constexpr int IDC_CHK_DESKTOP = 6003;
constexpr int IDC_CHK_STARTMENU = 6004;
constexpr int IDC_CHK_ASSOC = 6005;
constexpr int IDC_CHK_LAUNCH = 6006;
constexpr int IDC_PROGRESS = 6007;
constexpr int IDC_STATUS = 6008;
constexpr int IDC_BTN_INSTALL = 6009;
constexpr int IDC_BTN_CANCEL = 6010;

constexpr UINT WM_APP_INSTALL_PROGRESS = WM_APP + 10;
constexpr UINT WM_APP_INSTALL_DONE = WM_APP + 11;

#pragma pack(push, 1)
struct PayloadFooter {
    char magic[8];          // "PESETUP1"
    uint64_t payloadOffset; // byte offset of zip payload inside this exe
    uint64_t payloadSize;   // byte size of zip payload
};
#pragma pack(pop)

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

struct InstallOptions {
    std::wstring installDir;
    bool createDesktop = true;
    bool createStartMenu = true;
    bool registerMediaPlayer = true;
    bool launchAfter = true;
    bool silent = false;
};

struct InstallResult {
    bool ok = false;
    std::wstring error;
};

bool ExtractEmbeddedPayload(const std::wstring& selfExePath, const std::wstring& destDir, std::wstring& outErr) {
    std::ifstream ifs(selfExePath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        outErr = L"Could not open installer executable to read payload.";
        return false;
    }

    std::streamoff fileSize = ifs.tellg();
    if (fileSize < static_cast<std::streamoff>(sizeof(PayloadFooter))) {
        outErr = L"Installer payload is missing or corrupted.";
        return false;
    }

    ifs.seekg(fileSize - static_cast<std::streamoff>(sizeof(PayloadFooter)));
    PayloadFooter footer{};
    ifs.read(reinterpret_cast<char*>(&footer), sizeof(footer));
    if (!ifs.good() || std::memcmp(footer.magic, "PESETUP1", 8) != 0 ||
        footer.payloadSize == 0 ||
        footer.payloadOffset + footer.payloadSize + sizeof(PayloadFooter) != static_cast<uint64_t>(fileSize)) {
        outErr = L"Embedded installation payload not found. Please run the packaged PlayEverythingSetup.exe.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    if (ec) {
        outErr = L"Could not create installation folder:\n" + destDir;
        return false;
    }

    // Write payload zip to temp folder
    wchar_t tempDirBuf[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDirBuf);
    std::filesystem::path tempZip = std::filesystem::path(tempDirBuf) /
                                    (L"PlayEverything_payload_" + std::to_wstring(GetCurrentProcessId()) + L".zip");

    {
        std::ofstream ofs(tempZip, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            outErr = L"Could not create temporary extraction file.";
            return false;
        }
        ifs.seekg(static_cast<std::streamoff>(footer.payloadOffset));
        std::vector<char> chunk(1024 * 1024);
        uint64_t remaining = footer.payloadSize;
        while (remaining > 0) {
            size_t toRead = static_cast<size_t>(std::min<uint64_t>(remaining, chunk.size()));
            ifs.read(chunk.data(), static_cast<std::streamsize>(toRead));
            if (!ifs.good()) {
                outErr = L"Failed while reading embedded installer payload.";
                ofs.close();
                std::filesystem::remove(tempZip, ec);
                return false;
            }
            ofs.write(chunk.data(), static_cast<std::streamsize>(toRead));
            remaining -= toRead;
        }
    }

    // Extract with Windows built-in tar.exe (hidden, zero console flicker)
    wchar_t sysDir[MAX_PATH]{};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring tarExe = (std::filesystem::path(sysDir) / L"tar.exe").wstring();
    std::wstring cmd = L"\"" + tarExe + L"\" -xf \"" + tempZip.wstring() + L"\" -C \"" + destDir + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutableCmd = cmd;
    BOOL started = CreateProcessW(
        nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, destDir.c_str(), &si, &pi);

    DWORD exitCode = 1;
    if (started) {
        WaitForSingleObject(pi.hProcess, 120000);
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    std::filesystem::remove(tempZip, ec);

    if (!started || exitCode != 0) {
        outErr = L"Failed to extract Play Everything files to:\n" + destDir;
        return false;
    }

    // Sanity check extracted files
    std::filesystem::path exeCheck = std::filesystem::path(destDir) / L"PlayEverything.exe";
    std::filesystem::path dllCheck = std::filesystem::path(destDir) / L"libmpv-2.dll";
    if (!std::filesystem::exists(exeCheck) || !std::filesystem::exists(dllCheck)) {
        outErr = L"Installation files were not found after extraction.";
        return false;
    }

    return true;
}

InstallResult PerformInstall(const InstallOptions& opts, HWND progressHwnd) {
    InstallResult res{};

    auto postProgress = [&](int pct, const wchar_t* msg) {
        if (progressHwnd) {
            auto* textCopy = new std::wstring(msg);
            PostMessageW(progressHwnd, WM_APP_INSTALL_PROGRESS, static_cast<WPARAM>(pct),
                         reinterpret_cast<LPARAM>(textCopy));
        }
    };

    postProgress(10, L"Closing running instances...");
    playeverything::setup::CloseRunningPlayEverything();

    wchar_t selfBuf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);

    postProgress(25, L"Extracting Play Everything & universal codec engine...");
    if (!ExtractEmbeddedPayload(selfBuf, opts.installDir, res.error)) {
        return res;
    }

    postProgress(75, L"Creating shortcuts...");
    std::filesystem::path dir(opts.installDir);
    std::wstring exePath = (dir / L"PlayEverything.exe").wstring();
    std::wstring uninstPath = (dir / L"Uninstall.exe").wstring();

    if (opts.createDesktop) {
        playeverything::setup::CreateShortcut(
            playeverything::setup::GetDesktopShortcutPath(),
            exePath, L"", opts.installDir,
            L"Play Everything — Universal Audio & Video Player",
            exePath);
    }

    if (opts.createStartMenu) {
        std::filesystem::path smDir(playeverything::setup::GetStartMenuFolderPath());
        playeverything::setup::CreateShortcut(
            (smDir / L"Play Everything.lnk").wstring(),
            exePath, L"", opts.installDir,
            L"Play Everything — Universal Audio & Video Player",
            exePath);
        playeverything::setup::CreateShortcut(
            (smDir / L"Uninstall Play Everything.lnk").wstring(),
            uninstPath, L"", opts.installDir,
            L"Completely uninstall Play Everything",
            uninstPath);
    }

    postProgress(90, L"Registering Windows Media Player & Uninstaller...");
    playeverything::setup::RegisterWindowsIntegration(
        opts.installDir,
        opts.registerMediaPlayer,
        opts.createDesktop,
        opts.createStartMenu);

    postProgress(100, L"Installation complete!");
    res.ok = true;
    return res;
}

struct SetupUiState {
    HINSTANCE hInstance = nullptr;
    HWND hwnd = nullptr;
    HWND editDir = nullptr;
    HWND btnBrowse = nullptr;
    HWND chkDesktop = nullptr;
    HWND chkStartMenu = nullptr;
    HWND chkAssoc = nullptr;
    HWND chkLaunch = nullptr;
    HWND progress = nullptr;
    HWND lblStatus = nullptr;
    HWND btnInstall = nullptr;
    HWND btnCancel = nullptr;

    HICON iconBig = nullptr;
    HICON iconSmall = nullptr;
    HICON iconHeader = nullptr;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HFONT subFont = nullptr;

    bool installing = false;
    bool finished = false;
    std::wstring installedDir;
    std::thread worker;

    void SetInputsEnabled(BOOL en) {
        EnableWindow(editDir, en);
        EnableWindow(btnBrowse, en);
        EnableWindow(chkDesktop, en);
        EnableWindow(chkStartMenu, en);
        EnableWindow(chkAssoc, en);
        EnableWindow(btnInstall, en);
        EnableWindow(btnCancel, en);
    }

    void BrowseForFolder() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
            return;
        }
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"Choose Installation Folder for Play Everything");

        if (SUCCEEDED(dlg->Show(hwnd))) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    std::filesystem::path chosen(path);
                    CoTaskMemFree(path);
                    // Append "Play Everything" subfolder automatically if not already ending with it
                    if (_wcsicmp(chosen.filename().c_str(), L"Play Everything") != 0) {
                        chosen /= L"Play Everything";
                    }
                    SetWindowTextW(editDir, chosen.c_str());
                }
            }
        }
    }

    void StartInstall() {
        if (installing) return;
        if (finished) {
            if (SendMessageW(chkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED && !installedDir.empty()) {
                std::wstring exe = (std::filesystem::path(installedDir) / L"PlayEverything.exe").wstring();
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, installedDir.c_str(), SW_SHOWNORMAL);
            }
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return;
        }

        wchar_t dirBuf[MAX_PATH]{};
        GetWindowTextW(editDir, dirBuf, MAX_PATH);
        std::wstring dir(dirBuf);
        while (!dir.empty() && (dir.back() == L' ' || dir.back() == L'\\')) dir.pop_back();
        if (dir.empty()) {
            MessageBoxW(hwnd, L"Please enter a valid installation folder.", L"Play Everything Setup", MB_OK | MB_ICONWARNING);
            return;
        }

        InstallOptions opts{};
        opts.installDir = dir;
        opts.createDesktop = (SendMessageW(chkDesktop, BM_GETCHECK, 0, 0) == BST_CHECKED);
        opts.createStartMenu = (SendMessageW(chkStartMenu, BM_GETCHECK, 0, 0) == BST_CHECKED);
        opts.registerMediaPlayer = (SendMessageW(chkAssoc, BM_GETCHECK, 0, 0) == BST_CHECKED);
        opts.launchAfter = (SendMessageW(chkLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED);

        installing = true;
        installedDir = dir;
        SetInputsEnabled(FALSE);
        EnableWindow(chkLaunch, TRUE);
        SendMessageW(progress, PBM_SETPOS, 5, 0);

        if (worker.joinable()) worker.join();
        worker = std::thread([this, opts]() {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            auto* res = new InstallResult(PerformInstall(opts, hwnd));
            if (SUCCEEDED(hr)) CoUninitialize();
            PostMessageW(hwnd, WM_APP_INSTALL_DONE, 0, reinterpret_cast<LPARAM>(res));
        });
    }
};

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SetupUiState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
                switch (LOWORD(wp)) {
                    case IDC_BTN_BROWSE:
                        st->BrowseForFolder();
                        return 0;
                    case IDC_BTN_INSTALL:
                        st->StartInstall();
                        return 0;
                    case IDC_BTN_CANCEL:
                        if (!st->installing) PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return 0;
                    default:
                        break;
                }
            }
            break;

        case WM_APP_INSTALL_PROGRESS: {
            auto* txt = reinterpret_cast<std::wstring*>(lp);
            if (st) {
                SendMessageW(st->progress, PBM_SETPOS, wp, 0);
                if (txt) SetWindowTextW(st->lblStatus, txt->c_str());
            }
            delete txt;
            return 0;
        }

        case WM_APP_INSTALL_DONE: {
            auto* res = reinterpret_cast<InstallResult*>(lp);
            if (st && res) {
                st->installing = false;
                if (res->ok) {
                    st->finished = true;
                    SendMessageW(st->progress, PBM_SETPOS, 100, 0);
                    SetWindowTextW(st->lblStatus, L"Play Everything is installed and ready to use!");
                    SetWindowTextW(st->btnInstall, L"Finish");
                    EnableWindow(st->btnInstall, TRUE);
                    EnableWindow(st->btnCancel, FALSE);
                    SetFocus(st->btnInstall);
                } else {
                    SendMessageW(st->progress, PBM_SETPOS, 0, 0);
                    SetWindowTextW(st->lblStatus, L"Installation failed.");
                    st->SetInputsEnabled(TRUE);
                    MessageBoxW(hwnd, res->error.c_str(), L"Installation Error", MB_OK | MB_ICONERROR);
                }
            }
            delete res;
            return 0;
        }

        case WM_CLOSE:
            if (st && st->installing) return 0;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

InstallOptions ParseCliArgs() {
    InstallOptions opts{};
    opts.installDir = playeverything::setup::GetDefaultInstallDir();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (_wcsicmp(arg.c_str(), L"/S") == 0 || _wcsicmp(arg.c_str(), L"--silent") == 0) {
                opts.silent = true;
            } else if (_wcsicmp(arg.c_str(), L"/NODESKTOP") == 0) {
                opts.createDesktop = false;
            } else if (_wcsicmp(arg.c_str(), L"/NOSTARTMENU") == 0) {
                opts.createStartMenu = false;
            } else if (_wcsicmp(arg.c_str(), L"/NOASSOC") == 0) {
                opts.registerMediaPlayer = false;
            } else if (_wcsicmp(arg.c_str(), L"/NOLAUNCH") == 0) {
                opts.launchAfter = false;
            } else if (arg.size() > 3 && (_wcsnicmp(arg.c_str(), L"/D=", 3) == 0)) {
                opts.installDir = arg.substr(3);
                if (opts.installDir.size() >= 2 && opts.installDir.front() == L'"' && opts.installDir.back() == L'"') {
                    opts.installDir = opts.installDir.substr(1, opts.installDir.size() - 2);
                }
            }
        }
        LocalFree(argv);
    }
    return opts;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    InstallOptions cliOpts = ParseCliArgs();
    if (cliOpts.silent) {
        InstallResult res = PerformInstall(cliOpts, nullptr);
        if (res.ok && cliOpts.launchAfter) {
            std::wstring exe = (std::filesystem::path(cliOpts.installDir) / L"PlayEverything.exe").wstring();
            ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, cliOpts.installDir.c_str(), SW_SHOWNORMAL);
        }
        if (SUCCEEDED(comHr)) CoUninitialize();
        return res.ok ? 0 : 1;
    }

    SetupUiState st{};
    st.hInstance = hInstance;
    st.iconBig = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_SETUP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    st.iconSmall = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_SETUP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    st.iconHeader = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_SETUP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SetupWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = st.iconBig;
    wc.hIconSm = st.iconSmall;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kSetupClass;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    if (dpi == 0) dpi = 96;

    int winW = ScalePx(540, dpi);
    int winH = ScalePx(410, dpi);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        0, kSetupClass, L"Play Everything — Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sw - winW) / 2, (sh - winH) / 2, winW, winH,
        nullptr, nullptr, hInstance, &st);
    if (!hwnd) {
        if (SUCCEEDED(comHr)) CoUninitialize();
        return 1;
    }
    st.hwnd = hwnd;

    BOOL dark = TRUE;
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

    dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;

    st.font = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    st.titleFont = CreateFontW(-MulDiv(14, static_cast<int>(dpi), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    st.subFont = CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;
    int pad = ScalePx(20, dpi);

    // Header icon + title
    int iconSz = ScalePx(48, dpi);
    HWND pic = CreateWindowExW(
        0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
        pad, pad, iconSz, iconSz,
        hwnd, nullptr, hInstance, nullptr);
    if (st.iconHeader) {
        SendMessageW(pic, STM_SETICON, reinterpret_cast<WPARAM>(st.iconHeader), 0);
    }

    int textX = pad + iconSz + ScalePx(14, dpi);
    HWND lblTitle = CreateWindowExW(
        0, L"STATIC", L"Install Play Everything",
        WS_CHILD | WS_VISIBLE,
        textX, pad + ScalePx(2, dpi), cw - textX - pad, ScalePx(26, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lblTitle, WM_SETFONT, reinterpret_cast<WPARAM>(st.titleFont), TRUE);

    HWND lblSub = CreateWindowExW(
        0, L"STATIC", L"Universal Windows Audio && Video Player (all codecs + full uninstaller included)",
        WS_CHILD | WS_VISIBLE,
        textX, pad + ScalePx(30, dpi), cw - textX - pad, ScalePx(20, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lblSub, WM_SETFONT, reinterpret_cast<WPARAM>(st.subFont), TRUE);

    // Install location row
    int y = pad + iconSz + ScalePx(20, dpi);
    HWND lblDir = CreateWindowExW(
        0, L"STATIC", L"Installation Folder (choose any custom path):",
        WS_CHILD | WS_VISIBLE,
        pad, y, cw - pad * 2, ScalePx(20, dpi),
        hwnd, nullptr, hInstance, nullptr);
    SendMessageW(lblDir, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    y += ScalePx(24, dpi);
    int browseW = ScalePx(92, dpi);
    int gap = ScalePx(8, dpi);
    st.editDir = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", cliOpts.installDir.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        pad, y, cw - pad * 2 - browseW - gap, ScalePx(26, dpi),
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_DIR)), hInstance, nullptr);
    SendMessageW(st.editDir, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    st.btnBrowse = CreateWindowExW(
        0, L"BUTTON", L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cw - pad - browseW, y - 1, browseW, ScalePx(28, dpi),
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_BROWSE)), hInstance, nullptr);
    SendMessageW(st.btnBrowse, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    // Options checkboxes
    y += ScalePx(40, dpi);
    int chkH = ScalePx(22, dpi);
    int chkStep = ScalePx(26, dpi);

    st.chkDesktop = CreateWindowExW(
        0, L"BUTTON", L"Add Play Everything shortcut to Desktop",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        pad, y, cw - pad * 2, chkH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_DESKTOP)), hInstance, nullptr);
    SendMessageW(st.chkDesktop, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(st.chkDesktop, BM_SETCHECK, cliOpts.createDesktop ? BST_CHECKED : BST_UNCHECKED, 0);

    y += chkStep;
    st.chkStartMenu = CreateWindowExW(
        0, L"BUTTON", L"Add Play Everything && Uninstall shortcuts to Start Menu",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        pad, y, cw - pad * 2, chkH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_STARTMENU)), hInstance, nullptr);
    SendMessageW(st.chkStartMenu, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(st.chkStartMenu, BM_SETCHECK, cliOpts.createStartMenu ? BST_CHECKED : BST_UNCHECKED, 0);

    y += chkStep;
    st.chkAssoc = CreateWindowExW(
        0, L"BUTTON", L"Register as Windows Media Player (\"Open with\" for video && audio files)",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        pad, y, cw - pad * 2, chkH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_ASSOC)), hInstance, nullptr);
    SendMessageW(st.chkAssoc, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(st.chkAssoc, BM_SETCHECK, cliOpts.registerMediaPlayer ? BST_CHECKED : BST_UNCHECKED, 0);

    y += chkStep;
    st.chkLaunch = CreateWindowExW(
        0, L"BUTTON", L"Launch Play Everything after installation",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        pad, y, cw - pad * 2, chkH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_LAUNCH)), hInstance, nullptr);
    SendMessageW(st.chkLaunch, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(st.chkLaunch, BM_SETCHECK, cliOpts.launchAfter ? BST_CHECKED : BST_UNCHECKED, 0);

    // Progress bar + status
    y += ScalePx(36, dpi);
    st.lblStatus = CreateWindowExW(
        0, L"STATIC", L"Ready to install (~122 MB disk space required).",
        WS_CHILD | WS_VISIBLE,
        pad, y, cw - pad * 2, ScalePx(18, dpi),
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), hInstance, nullptr);
    SendMessageW(st.lblStatus, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    y += ScalePx(22, dpi);
    st.progress = CreateWindowExW(
        0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        pad, y, cw - pad * 2, ScalePx(18, dpi),
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROGRESS)), hInstance, nullptr);
    SendMessageW(st.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    // Footer buttons
    int btnW = ScalePx(96, dpi);
    int btnH = ScalePx(30, dpi);
    int btnY = ch - pad - btnH;

    st.btnCancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        cw - pad - btnW, btnY, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CANCEL)), hInstance, nullptr);
    SendMessageW(st.btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    st.btnInstall = CreateWindowExW(
        0, L"BUTTON", L"Install",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        cw - pad - btnW * 2 - gap, btnY, btnW, btnH,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_INSTALL)), hInstance, nullptr);
    SendMessageW(st.btnInstall, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (st.worker.joinable()) st.worker.join();
    if (st.font) DeleteObject(st.font);
    if (st.titleFont) DeleteObject(st.titleFont);
    if (st.subFont) DeleteObject(st.subFont);
    if (st.iconBig) DestroyIcon(st.iconBig);
    if (st.iconSmall) DestroyIcon(st.iconSmall);
    if (st.iconHeader) DestroyIcon(st.iconHeader);

    if (SUCCEEDED(comHr)) CoUninitialize();
    return 0;
}
