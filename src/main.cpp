#include "ui/MainWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Friendly upfront check for bundled libmpv-2.dll
    HMODULE mpvCheck = LoadLibraryW(L"libmpv-2.dll");
    if (!mpvCheck) {
        MessageBoxW(
            nullptr,
            L"Could not find libmpv-2.dll.\n\nPlease keep libmpv-2.dll in the same folder as PlayEverything.exe.",
            L"Play Everything — Missing Codec Engine",
            MB_OK | MB_ICONERROR);
        return 1;
    }
    FreeLibrary(mpvCheck);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::wstring initialFile;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 2 && argv[1] && argv[1][0] != L'\0') {
            initialFile = argv[1];
        }
        LocalFree(argv);
    }

    int exitCode = 0;
    {
        playeverything::MainWindow window;
        if (window.Create(hInstance, nCmdShow, initialFile)) {
            exitCode = window.RunMessageLoop();
        } else {
            exitCode = 1;
        }
    }

    if (SUCCEEDED(comHr)) {
        CoUninitialize();
    }
    return exitCode;
}
