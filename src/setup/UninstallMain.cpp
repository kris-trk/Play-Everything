#include "setup/WindowsIntegration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

namespace {

bool IsSilentArg() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"/S") == 0 || _wcsicmp(argv[i], L"--silent") == 0) {
                silent = true;
                break;
            }
        }
        LocalFree(argv);
    }
    return silent;
}

void ScheduleSelfAndDirDeletion(const std::wstring& selfExePath, const std::wstring& installDir) {
    // Runs silently after Uninstall.exe exits to remove Uninstall.exe and the install directory completely
    std::wstring cmd = L"cmd.exe /C ping 127.0.0.1 -n 2 >nul & del /f /q \"" + selfExePath +
                       L"\" >nul 2>&1 & rmdir /s /q \"" + installDir + L"\" >nul 2>&1";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmd;
    if (CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, L"C:\\", &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    bool silent = IsSilentArg();

    wchar_t selfBuf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfBuf, MAX_PATH);
    std::filesystem::path selfPath(selfBuf);
    std::filesystem::path installDir = selfPath.parent_path();

    if (!silent) {
        std::wstring msg =
            L"Are you sure you want to completely uninstall Play Everything?\n\n"
            L"This will permanently delete:\n"
            L"  • All application files and settings in:\n    " + installDir.wstring() + L"\n"
            L"  • Desktop and Start Menu shortcuts\n"
            L"  • Windows \"Open with\" and Default Apps registrations\n"
            L"  • Windows Installed Apps entry";

        int ans = MessageBoxW(nullptr, msg.c_str(), L"Uninstall Play Everything",
                              MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (ans != IDYES) {
            if (SUCCEEDED(comHr)) CoUninitialize();
            return 0;
        }
    }

    // 1. Stop any running PlayEverything.exe so files aren't locked
    playeverything::setup::CloseRunningPlayEverything();

    // 2. Remove shortcuts, Open With associations, Default Apps capabilities, and Uninstall registry entry
    std::wstring registeredDir;
    playeverything::setup::UnregisterAllWindowsIntegration(&registeredDir);

    // 3. Delete all files inside installDir except Uninstall.exe itself
    auto cleanFolder = [&](const std::filesystem::path& dir) {
        if (dir.empty() || !std::filesystem::exists(dir)) return;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (_wcsicmp(entry.path().c_str(), selfPath.c_str()) == 0) {
                continue;
            }
            std::filesystem::remove_all(entry.path(), ec);
        }
    };

    cleanFolder(installDir);
    if (!registeredDir.empty()) {
        std::filesystem::path regPath(registeredDir);
        if (_wcsicmp(regPath.c_str(), installDir.c_str()) != 0) {
            std::error_code ec;
            std::filesystem::remove_all(regPath, ec);
        }
    }

    if (!silent) {
        MessageBoxW(
            nullptr,
            L"Play Everything and all its settings, shortcuts, and Windows registrations have been completely removed.",
            L"Uninstall Complete — Play Everything",
            MB_OK | MB_ICONINFORMATION);
    }

    // 4. Schedule Uninstall.exe and the now-empty install folder for deletion right after exit
    ScheduleSelfAndDirDeletion(selfPath.wstring(), installDir.wstring());

    if (SUCCEEDED(comHr)) CoUninitialize();
    return 0;
}
