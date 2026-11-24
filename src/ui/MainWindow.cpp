#include "ui/MainWindow.h"

#include "ui/CustomSpeedDialog.h"
#include "ui/KeybindsDialog.h"
#include "ui/MediaInfoDialog.h"
#include "ui/Resource.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace playeverything {

namespace {

constexpr wchar_t kMainClassName[] = L"PlayEverythingMainWindow";
constexpr wchar_t kVideoHostClassName[] = L"PlayEverythingVideoHost";
constexpr UINT_PTR kTimerFullscreenHide = 101;
constexpr UINT kFullscreenHideMs = 2500;

std::wstring MenuLabel(const std::wstring& label, const std::wstring& shortcut) {
    if (shortcut.empty()) return label;
    return label + L"\t" + shortcut;
}

bool SpeedEquals(double a, double b) {
    return std::abs(a - b) < 0.005;
}

}  // namespace

MainWindow::~MainWindow() {
    thumbnails_.Stop();
    engine_.Shutdown();
    if (cursorHidden_) {
        ShowCursor(TRUE);
        cursorHidden_ = false;
    }
    if (isFullscreen_ && menuBar_) {
        DestroyMenu(menuBar_);
        menuBar_ = nullptr;
    }
    if (appIconBig_) {
        DestroyIcon(appIconBig_);
        appIconBig_ = nullptr;
    }
    if (appIconSmall_) {
        DestroyIcon(appIconSmall_);
        appIconSmall_ = nullptr;
    }
}

std::wstring MainWindow::KeybindsFilePath() const {
    wchar_t exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0) return L"keybinds.json";
    std::filesystem::path p(exePath);
    return (p.parent_path() / L"keybinds.json").wstring();
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile) {
    hInstance_ = hInstance;

    keybinds_.LoadFromFile(KeybindsFilePath());

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()))) {
        MessageBoxW(nullptr, L"Failed to initialize Direct2D.", L"Play Everything", MB_OK | MB_ICONERROR);
        return false;
    }
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
        MessageBoxW(nullptr, L"Failed to initialize DirectWrite.", L"Play Everything", MB_OK | MB_ICONERROR);
        return false;
    }

    appIconBig_ = static_cast<HICON>(LoadImageW(
        hInstance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    appIconSmall_ = static_cast<HICON>(LoadImageW(
        hInstance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &MainWindow::WndProc;
    wc.hInstance = hInstance_;
    wc.hIcon = appIconBig_;
    wc.hIconSm = appIconSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(14, 16, 21));
    wc.lpszClassName = kMainClassName;
    RegisterClassExW(&wc);

    WNDCLASSEXW vwc{};
    vwc.cbSize = sizeof(vwc);
    vwc.lpfnWndProc = DefWindowProcW;
    vwc.hInstance = hInstance_;
    vwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    vwc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    vwc.lpszClassName = kVideoHostClassName;
    RegisterClassExW(&vwc);

    BuildMenu();

    hwnd_ = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        kMainClassName,
        L"Play Everything",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 640,
        nullptr, menuBar_, hInstance_, this);
    if (!hwnd_) return false;

    BOOL dark = TRUE;
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    DwmSetWindowAttribute(hwnd_, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

    if (appIconBig_) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIconBig_));
    }
    if (appIconSmall_) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSmall_));
    }

    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;

    // Video host HWND: disabled so mouse clicks, double-clicks, wheel, and drag-and-drop go cleanly to MainWindow
    videoHostHwnd_ = CreateWindowExW(
        0, kVideoHostClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        hwnd_, nullptr, hInstance_, nullptr);
    if (!videoHostHwnd_) return false;

    if (!emptyAudioView_.Create(
            hwnd_, hInstance_, d2dFactory_.Get(), dwriteFactory_.Get(),
            [this]() { OpenFileDialog(); },
            [this]() { ToggleFullscreen(); })) {
        return false;
    }

    PlaylistPanelCallbacks plCb{};
    plCb.onPlayIndex = [this](size_t idx) { PlayPlaylistIndex(idx); };
    plCb.onAddFiles = [this]() { AddPlaylistFilesDialog(); };
    plCb.onRemoveIndex = [this](size_t idx) {
        playlist_.RemoveAt(idx);
        playlistPanel_.Refresh(playlist_);
        RefreshMenuState();
    };
    plCb.onMoveUp = [this](size_t idx) {
        playlist_.MoveUp(idx);
        playlistPanel_.Refresh(playlist_);
    };
    plCb.onMoveDown = [this](size_t idx) {
        playlist_.MoveDown(idx);
        playlistPanel_.Refresh(playlist_);
    };
    plCb.onClear = [this]() {
        playlist_.Clear();
        playlistPanel_.Refresh(playlist_);
        RefreshMenuState();
    };
    plCb.onClose = [this]() {
        if (playlistVisible_) TogglePlaylistPanel();
    };
    if (!playlistPanel_.Create(hwnd_, hInstance_, std::move(plCb))) {
        return false;
    }

    previewPopup_.Create(hwnd_, hInstance_);

    TransportCallbacks cb{};
    cb.onPlay = [this]() { engine_.Play(); };
    cb.onPause = [this]() { engine_.Pause(); };
    cb.onRestart = [this]() { engine_.Restart(); };
    cb.onBack15 = [this]() { engine_.SeekRelative(-15.0); };
    cb.onForward15 = [this]() { engine_.SeekRelative(15.0); };
    cb.onToggleLoop = [this]() {
        engine_.ToggleLoop();
        RefreshMenuState();
    };
    cb.onToggleShuffle = [this]() { ToggleShuffle(); };
    cb.onToggleMute = [this]() { engine_.ToggleMute(); };
    cb.onToggleFullscreen = [this]() { ToggleFullscreen(); };
    cb.onSpeedClick = [this]() { ShowSpeedPopupMenu(); };
    cb.onSeek = [this](double sec, bool exact) { engine_.SeekAbsolute(sec, exact); };
    cb.onVolume = [this](double vol) { engine_.SetVolume(vol); };
    cb.onTimelineHover = [this](double hoverSec, int screenX, int barTopScreenY) {
        const auto& st = engine_.State();
        if (!st.loaded || st.duration <= 0.0) {
            previewPopup_.Hide();
            return;
        }
        RECT winRc{};
        GetWindowRect(hwnd_, &winRc);
        previewPopup_.ShowAt(screenX, barTopScreenY, hoverSec, st.duration, st.hasVideo, winRc);
        if (st.hasVideo) {
            thumbnails_.RequestFrame(hoverSec);
        }
    };
    cb.onTimelineHoverEnd = [this]() { previewPopup_.Hide(); };

    if (!transportBar_.Create(hwnd_, hInstance_, d2dFactory_.Get(), dwriteFactory_.Get(), &keybinds_, std::move(cb))) {
        return false;
    }

    if (!engine_.Initialize(videoHostHwnd_, hwnd_, WM_APP_MPV_WAKEUP, false)) {
        MessageBoxW(hwnd_, engine_.State().lastError.c_str(), L"Play Everything", MB_OK | MB_ICONERROR);
        return false;
    }

    thumbnails_.Start(hwnd_, WM_APP_PREVIEW_READY);

    LayoutChildren();
    UpdateViewportVisibility();
    RefreshMenuState();

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);

    if (!initialFile.empty()) {
        OpenMediaPath(initialFile, true);
    }

    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void MainWindow::BuildMenu() {
    menuBar_ = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_OPEN,
                MenuLabel(L"&Open File...", keybinds_.GetPrimaryShortcut(PlayerAction::OpenFile)).c_str());
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_CLOSE, L"&Close Media");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"E&xit");

    speedMenu_ = CreatePopupMenu();
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_025, L"0.25x");
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_050, L"0.5x");
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_075, L"0.75x");
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_100, L"1x (Normal)");
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_200, L"2x");
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_300, L"3x");
    AppendMenuW(speedMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(speedMenu_, MF_STRING, IDM_SPEED_CUSTOM, L"Custom...");

    playbackMenu_ = CreatePopupMenu();
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_TOGGLE,
                MenuLabel(L"&Play / Pause", keybinds_.GetPrimaryShortcut(PlayerAction::PlayPause)).c_str());
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_RESTART,
                MenuLabel(L"&Restart", keybinds_.GetPrimaryShortcut(PlayerAction::Restart)).c_str());
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_BACK15,
                MenuLabel(L"&Backward 15s", keybinds_.GetPrimaryShortcut(PlayerAction::SeekBack15)).c_str());
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_FWD15,
                MenuLabel(L"&Forward 15s", keybinds_.GetPrimaryShortcut(PlayerAction::SeekForward15)).c_str());
    AppendMenuW(playbackMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(playbackMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(speedMenu_), L"&Speed");
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_LOOP,
                MenuLabel(L"&Loop", keybinds_.GetPrimaryShortcut(PlayerAction::ToggleLoop)).c_str());
    AppendMenuW(playbackMenu_, MF_STRING, IDM_PLAY_FULLSCREEN,
                MenuLabel(L"F&ullscreen", keybinds_.GetPrimaryShortcut(PlayerAction::ToggleFullscreen)).c_str());

    playlistMenu_ = CreatePopupMenu();
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_TOGGLE,
                MenuLabel(L"&Show Playlist Panel", keybinds_.GetPrimaryShortcut(PlayerAction::TogglePlaylist)).c_str());
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_ADD, L"&Add Files...");
    AppendMenuW(playlistMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_SHUFFLE,
                MenuLabel(L"Sh&uffle", keybinds_.GetPrimaryShortcut(PlayerAction::ToggleShuffle)).c_str());
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_PREV,
                MenuLabel(L"&Previous Track", keybinds_.GetPrimaryShortcut(PlayerAction::PrevTrack)).c_str());
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_NEXT,
                MenuLabel(L"&Next Track", keybinds_.GetPrimaryShortcut(PlayerAction::NextTrack)).c_str());
    AppendMenuW(playlistMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(playlistMenu_, MF_STRING, IDM_PLAYLIST_CLEAR, L"&Clear Playlist");

    HMENU settingsMenu = CreatePopupMenu();
    AppendMenuW(settingsMenu, MF_STRING, IDM_SETTINGS_KEYBINDS, L"&Keybinds...");
    AppendMenuW(settingsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(settingsMenu, MF_STRING, IDM_SETTINGS_UNINSTALL, L"&Uninstall Play Everything...");

    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"&File");
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(playbackMenu_), L"&Playback");
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(playlistMenu_), L"Play&list");
    AppendMenuW(menuBar_, MF_STRING, IDM_INFO_SHOW, L"&Info");
    AppendMenuW(menuBar_, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), L"&Settings");
}

void MainWindow::RefreshMenuState() {
    if (!menuBar_) return;

    auto setText = [this](UINT id, const std::wstring& label, PlayerAction act) {
        std::wstring full = MenuLabel(label, keybinds_.GetPrimaryShortcut(act));
        MENUITEMINFOW mii{};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING;
        mii.dwTypeData = const_cast<LPWSTR>(full.c_str());
        SetMenuItemInfoW(menuBar_, id, FALSE, &mii);
    };

    setText(IDM_FILE_OPEN, L"&Open File...", PlayerAction::OpenFile);
    setText(IDM_PLAY_TOGGLE, L"&Play / Pause", PlayerAction::PlayPause);
    setText(IDM_PLAY_RESTART, L"&Restart", PlayerAction::Restart);
    setText(IDM_PLAY_BACK15, L"&Backward 15s", PlayerAction::SeekBack15);
    setText(IDM_PLAY_FWD15, L"&Forward 15s", PlayerAction::SeekForward15);
    setText(IDM_PLAY_LOOP, L"&Loop", PlayerAction::ToggleLoop);
    setText(IDM_PLAY_FULLSCREEN, L"F&ullscreen", PlayerAction::ToggleFullscreen);
    setText(IDM_PLAYLIST_TOGGLE, L"&Show Playlist Panel", PlayerAction::TogglePlaylist);
    setText(IDM_PLAYLIST_SHUFFLE, L"Sh&uffle", PlayerAction::ToggleShuffle);
    setText(IDM_PLAYLIST_PREV, L"&Previous Track", PlayerAction::PrevTrack);
    setText(IDM_PLAYLIST_NEXT, L"&Next Track", PlayerAction::NextTrack);

    const auto& st = engine_.State();
    EnableMenuItem(menuBar_, IDM_FILE_CLOSE, MF_BYCOMMAND | (st.loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAY_TOGGLE, MF_BYCOMMAND | (st.loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAY_RESTART, MF_BYCOMMAND | (st.loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAY_BACK15, MF_BYCOMMAND | (st.loaded ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAY_FWD15, MF_BYCOMMAND | (st.loaded ? MF_ENABLED : MF_GRAYED));

    CheckMenuItem(menuBar_, IDM_PLAY_LOOP, MF_BYCOMMAND | (st.loopEnabled ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_PLAY_FULLSCREEN, MF_BYCOMMAND | (isFullscreen_ ? MF_CHECKED : MF_UNCHECKED));

    // Speed checkmarks
    const double spd = st.speed;
    bool is025 = SpeedEquals(spd, 0.25);
    bool is050 = SpeedEquals(spd, 0.50);
    bool is075 = SpeedEquals(spd, 0.75);
    bool is100 = SpeedEquals(spd, 1.00);
    bool is200 = SpeedEquals(spd, 2.00);
    bool is300 = SpeedEquals(spd, 3.00);
    bool isCustom = !(is025 || is050 || is075 || is100 || is200 || is300);

    CheckMenuItem(menuBar_, IDM_SPEED_025, MF_BYCOMMAND | (is025 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_050, MF_BYCOMMAND | (is050 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_075, MF_BYCOMMAND | (is075 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_100, MF_BYCOMMAND | (is100 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_200, MF_BYCOMMAND | (is200 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_300, MF_BYCOMMAND | (is300 ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_SPEED_CUSTOM, MF_BYCOMMAND | (isCustom ? MF_CHECKED : MF_UNCHECKED));

    std::wstring customLabel = L"Custom...";
    if (isCustom) {
        wchar_t buf[64]{};
        std::swprintf(buf, std::size(buf), L"Custom (%.2gx)...", spd);
        customLabel = buf;
    }
    MENUITEMINFOW cmii{};
    cmii.cbSize = sizeof(cmii);
    cmii.fMask = MIIM_STRING;
    cmii.dwTypeData = const_cast<LPWSTR>(customLabel.c_str());
    SetMenuItemInfoW(menuBar_, IDM_SPEED_CUSTOM, FALSE, &cmii);

    // Playlist menu state
    CheckMenuItem(menuBar_, IDM_PLAYLIST_TOGGLE, MF_BYCOMMAND | (playlistVisible_ ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menuBar_, IDM_PLAYLIST_SHUFFLE, MF_BYCOMMAND | (playlist_.IsShuffleEnabled() ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menuBar_, IDM_PLAYLIST_PREV, MF_BYCOMMAND | (playlist_.HasPrevious() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAYLIST_NEXT, MF_BYCOMMAND | (playlist_.HasNext() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menuBar_, IDM_PLAYLIST_CLEAR, MF_BYCOMMAND | (!playlist_.Empty() ? MF_ENABLED : MF_GRAYED));

    if (!isFullscreen_) {
        DrawMenuBar(hwnd_);
    }
}

void MainWindow::LayoutChildren() {
    if (!hwnd_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = std::max<int>(1, rc.right - rc.left);
    int h = std::max<int>(1, rc.bottom - rc.top);

    int barH = transportBar_.PixelHeight();
    bool showBar = !(isFullscreen_ && controlsHidden_);
    int viewH = showBar ? std::max<int>(1, h - barH) : h;

    bool showPlaylist = playlistVisible_ && !isFullscreen_;
    int plW = showPlaylist ? std::min(playlistPanel_.PixelWidth(), std::max(160, w / 3)) : 0;
    int viewW = std::max<int>(1, w - plW);

    if (videoHostHwnd_) {
        SetWindowPos(videoHostHwnd_, nullptr, 0, 0, viewW, viewH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (emptyAudioView_.Handle()) {
        SetWindowPos(emptyAudioView_.Handle(), HWND_TOP, 0, 0, viewW, viewH, SWP_NOACTIVATE);
    }
    if (playlistPanel_.Handle()) {
        if (showPlaylist) {
            SetWindowPos(playlistPanel_.Handle(), HWND_TOP, viewW, 0, plW, viewH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        } else {
            ShowWindow(playlistPanel_.Handle(), SW_HIDE);
        }
    }
    if (transportBar_.Handle()) {
        if (showBar) {
            SetWindowPos(transportBar_.Handle(), HWND_TOP, 0, viewH, w, barH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        } else {
            ShowWindow(transportBar_.Handle(), SW_HIDE);
        }
    }
}

void MainWindow::UpdateWindowTitle() {
    const auto& st = engine_.State();
    std::wstring title = L"Play Everything";
    if (st.loaded && !st.mediaTitle.empty()) {
        title = st.mediaTitle + L" — Play Everything";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::UpdateViewportVisibility() {
    const auto& st = engine_.State();
    bool showOverlay = !st.loaded || !st.hasVideo;
    if (emptyAudioView_.Handle()) {
        ShowWindow(emptyAudioView_.Handle(), showOverlay ? SW_SHOW : SW_HIDE);
    }
    if (videoHostHwnd_) {
        ShowWindow(videoHostHwnd_, showOverlay ? SW_HIDE : SW_SHOW);
    }
}

void MainWindow::ExecuteAction(PlayerAction action) {
    switch (action) {
        case PlayerAction::PlayPause:
            if (engine_.State().loaded) engine_.TogglePlayPause();
            else OpenFileDialog();
            break;
        case PlayerAction::Play:
            engine_.Play();
            break;
        case PlayerAction::Pause:
            engine_.Pause();
            break;
        case PlayerAction::Restart:
            engine_.Restart();
            break;
        case PlayerAction::SeekBack15:
            engine_.SeekRelative(-15.0);
            break;
        case PlayerAction::SeekForward15:
            engine_.SeekRelative(15.0);
            break;
        case PlayerAction::ToggleLoop:
            engine_.ToggleLoop();
            RefreshMenuState();
            break;
        case PlayerAction::ToggleShuffle:
            ToggleShuffle();
            break;
        case PlayerAction::VolumeUp:
            engine_.AdjustVolume(5.0);
            break;
        case PlayerAction::VolumeDown:
            engine_.AdjustVolume(-5.0);
            break;
        case PlayerAction::ToggleMute:
            engine_.ToggleMute();
            break;
        case PlayerAction::ToggleFullscreen:
            ToggleFullscreen();
            break;
        case PlayerAction::OpenFile:
            OpenFileDialog();
            break;
        case PlayerAction::NextTrack:
            PlayNextTrack();
            break;
        case PlayerAction::PrevTrack:
            PlayPrevTrack();
            break;
        case PlayerAction::TogglePlaylist:
            TogglePlaylistPanel();
            break;
        case PlayerAction::ShowInfo:
            OpenMediaInfoDialog();
            break;
        default:
            break;
    }
}

std::vector<std::wstring> MainWindow::ShowMediaFilePicker(const std::wstring& title, bool multiSelect) {
    std::vector<std::wstring> results;
    Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) {
        return results;
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"All Media Files",
         L"*.mp4;*.mkv;*.avi;*.mov;*.webm;*.wmv;*.flv;*.m4v;*.ts;*.m2ts;*.mpg;*.mpeg;*.3gp;*.ogv;"
         L"*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg;*.opus;*.wma;*.alac;*.ape;*.ac3;*.dts;*.wv;*.aiff;*.mka"},
        {L"Video Files", L"*.mp4;*.mkv;*.avi;*.mov;*.webm;*.wmv;*.flv;*.m4v;*.ts;*.m2ts;*.mpg;*.mpeg;*.3gp;*.ogv"},
        {L"Audio Files", L"*.mp3;*.flac;*.wav;*.aac;*.m4a;*.ogg;*.opus;*.wma;*.alac;*.ape;*.ac3;*.dts;*.wv;*.aiff;*.mka"},
        {L"All Files (*.*)", L"*.*"},
    };
    dlg->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dlg->SetFileTypeIndex(1);
    dlg->SetTitle(title.c_str());

    if (multiSelect) {
        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts))) {
            dlg->SetOptions(opts | FOS_ALLOWMULTISELECT);
        }
    }

    if (SUCCEEDED(dlg->Show(hwnd_))) {
        if (multiSelect) {
            Microsoft::WRL::ComPtr<IShellItemArray> items;
            if (SUCCEEDED(dlg->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    Microsoft::WRL::ComPtr<IShellItem> item;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                            results.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                    }
                }
            }
        } else {
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    results.emplace_back(path);
                    CoTaskMemFree(path);
                }
            }
        }
    }
    return results;
}

void MainWindow::OpenFileDialog() {
    previewPopup_.Hide();
    auto files = ShowMediaFilePicker(L"Open Video or Audio File(s)", true);
    if (files.empty()) return;

    if (files.size() == 1) {
        OpenMediaPath(files[0], true);
    } else {
        int firstIdx = playlist_.AddFiles(files);
        if (firstIdx >= 0) {
            PlayPlaylistIndex(static_cast<size_t>(firstIdx));
            if (!playlistVisible_) {
                playlistVisible_ = true;
                LayoutChildren();
                RefreshMenuState();
            }
        }
    }
}

void MainWindow::AddPlaylistFilesDialog() {
    previewPopup_.Hide();
    auto files = ShowMediaFilePicker(L"Add Files to Playlist", true);
    if (files.empty()) return;

    bool wasEmpty = !engine_.State().loaded && playlist_.Empty();
    int firstIdx = playlist_.AddFiles(files);
    playlistPanel_.Refresh(playlist_);
    RefreshMenuState();

    if (wasEmpty && firstIdx >= 0) {
        PlayPlaylistIndex(static_cast<size_t>(firstIdx));
    }
}

void MainWindow::OpenMediaPath(const std::wstring& path, bool addToPlaylist) {
    if (path.empty()) return;
    previewPopup_.Hide();
    prevEofReached_ = false;
    if (addToPlaylist) {
        playlist_.AddOrSelectFile(path);
        playlistPanel_.Refresh(playlist_);
    }
    thumbnails_.ClearMedia();
    engine_.OpenFile(path);
    UpdateWindowTitle();
    RefreshMenuState();
}

void MainWindow::OpenKeybindsDialog() {
    previewPopup_.Hide();
    bool wasFullscreen = isFullscreen_;
    if (wasFullscreen) {
        WakeFullscreenControls();
        KillTimer(hwnd_, kTimerFullscreenHide);
    }

    if (ShowKeybindsDialog(hwnd_, hInstance_, keybinds_)) {
        keybinds_.SaveToFile(KeybindsFilePath());
        RefreshMenuState();
        transportBar_.RefreshTooltips();
    }

    if (wasFullscreen) {
        SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
    }
}

void MainWindow::LaunchUninstaller() {
    previewPopup_.Hide();

    wchar_t exeBuf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
    std::filesystem::path uninstPath = exeDir / L"Uninstall.exe";

    if (!std::filesystem::exists(uninstPath)) {
        // Fallback: check registered InstallLocation in HKCU
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlayEverything",
                          0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
            wchar_t dirBuf[MAX_PATH]{};
            DWORD bytes = sizeof(dirBuf);
            DWORD type = 0;
            if (RegQueryValueExW(key, L"InstallLocation", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(dirBuf), &bytes) == ERROR_SUCCESS &&
                type == REG_SZ) {
                std::filesystem::path regUninst = std::filesystem::path(dirBuf) / L"Uninstall.exe";
                if (std::filesystem::exists(regUninst)) {
                    uninstPath = regUninst;
                }
            }
            RegCloseKey(key);
        }
    }

    if (std::filesystem::exists(uninstPath)) {
        std::wstring workDir = uninstPath.parent_path().wstring();
        ShellExecuteW(hwnd_, L"open", uninstPath.c_str(), nullptr, workDir.c_str(), SW_SHOWNORMAL);
    } else {
        MessageBoxW(
            hwnd_,
            L"Uninstall.exe was not found in the application folder.\n\n"
            L"If you are running the portable copy, you can simply delete its folder.",
            L"Uninstall Play Everything",
            MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindow::OpenMediaInfoDialog() {
    previewPopup_.Hide();
    bool wasFullscreen = isFullscreen_;
    if (wasFullscreen) {
        WakeFullscreenControls();
        KillTimer(hwnd_, kTimerFullscreenHide);
    }

    auto info = engine_.QueryMediaInfo();
    ShowMediaInfoDialog(hwnd_, hInstance_, info);

    if (wasFullscreen) {
        SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
    }
}

void MainWindow::OpenCustomSpeedDialog() {
    previewPopup_.Hide();
    bool wasFullscreen = isFullscreen_;
    if (wasFullscreen) {
        WakeFullscreenControls();
        KillTimer(hwnd_, kTimerFullscreenHide);
    }

    auto val = ShowCustomSpeedDialog(hwnd_, hInstance_, engine_.State().speed);
    if (val.has_value()) {
        ApplySpeed(*val);
    }

    if (wasFullscreen) {
        SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
    }
}

void MainWindow::ShowSpeedPopupMenu() {
    if (!speedMenu_) return;
    previewPopup_.Hide();
    RefreshMenuState();

    POINT pt{};
    GetCursorPos(&pt);
    TrackPopupMenu(speedMenu_, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
}

void MainWindow::ApplySpeed(double speed) {
    engine_.SetSpeed(speed);
    transportBar_.SetState(engine_.State(), isFullscreen_, playlist_.IsShuffleEnabled());
    RefreshMenuState();
}

void MainWindow::TogglePlaylistPanel() {
    playlistVisible_ = !playlistVisible_;
    playlistPanel_.Refresh(playlist_);
    LayoutChildren();
    RefreshMenuState();
}

void MainWindow::ToggleShuffle() {
    playlist_.ToggleShuffle();
    transportBar_.SetState(engine_.State(), isFullscreen_, playlist_.IsShuffleEnabled());
    RefreshMenuState();
}

void MainWindow::PlayPlaylistIndex(size_t index) {
    const auto& items = playlist_.Items();
    if (index >= items.size()) return;
    playlist_.SetCurrentIndex(static_cast<int>(index));
    playlistPanel_.Refresh(playlist_);
    OpenMediaPath(items[index].path, false);
}

void MainWindow::PlayNextTrack() {
    auto nextPath = playlist_.Next();
    if (!nextPath.has_value()) return;
    playlistPanel_.Refresh(playlist_);
    OpenMediaPath(*nextPath, false);
}

void MainWindow::PlayPrevTrack() {
    auto prevPath = playlist_.Previous();
    if (!prevPath.has_value()) return;
    playlistPanel_.Refresh(playlist_);
    OpenMediaPath(*prevPath, false);
}

void MainWindow::ToggleFullscreen() {
    SetFullscreen(!isFullscreen_);
}

void MainWindow::SetFullscreen(bool enable) {
    if (isFullscreen_ == enable) return;
    previewPopup_.Hide();
    isFullscreen_ = enable;

    if (enable) {
        savedStyle_ = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
        savedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd_, &savedPlacement_);

        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);

        SetMenu(hwnd_, nullptr);
        SetWindowLongW(hwnd_, GWL_STYLE, (savedStyle_ & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(
            hwnd_, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        controlsHidden_ = false;
        SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
    } else {
        KillTimer(hwnd_, kTimerFullscreenHide);
        if (cursorHidden_) {
            ShowCursor(TRUE);
            cursorHidden_ = false;
        }
        controlsHidden_ = false;

        SetWindowLongW(hwnd_, GWL_STYLE, savedStyle_);
        SetMenu(hwnd_, menuBar_);
        SetWindowPlacement(hwnd_, &savedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }

    LayoutChildren();
    transportBar_.SetState(engine_.State(), isFullscreen_, playlist_.IsShuffleEnabled());
    RefreshMenuState();
}

void MainWindow::WakeFullscreenControls() {
    if (!isFullscreen_) return;
    if (cursorHidden_) {
        ShowCursor(TRUE);
        cursorHidden_ = false;
    }
    if (controlsHidden_) {
        controlsHidden_ = false;
        LayoutChildren();
    }
    SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
}

void MainWindow::HideFullscreenControls() {
    if (!isFullscreen_) return;
    KillTimer(hwnd_, kTimerFullscreenHide);

    POINT pt{};
    if (GetCursorPos(&pt) && transportBar_.Handle() && IsWindowVisible(transportBar_.Handle())) {
        RECT barRc{};
        GetWindowRect(transportBar_.Handle(), &barRc);
        if (PtInRect(&barRc, pt)) {
            SetTimer(hwnd_, kTimerFullscreenHide, kFullscreenHideMs, nullptr);
            return;
        }
    }

    previewPopup_.Hide();
    if (!controlsHidden_) {
        controlsHidden_ = true;
        LayoutChildren();
    }
    if (!cursorHidden_) {
        ShowCursor(FALSE);
        cursorHidden_ = true;
    }
}

bool MainWindow::HandleKeyDown(UINT vk, bool isRepeat) {
    if (vk == VK_ESCAPE && isFullscreen_) {
        SetFullscreen(false);
        return true;
    }
    if (KeyCombo::IsModifierKey(vk)) {
        return false;
    }

    KeyCombo combo{};
    combo.vk = vk;
    combo.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    combo.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    combo.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    auto action = keybinds_.FindAction(combo);
    if (!action.has_value()) return false;

    if (isRepeat) {
        switch (*action) {
            case PlayerAction::SeekBack15:
            case PlayerAction::SeekForward15:
            case PlayerAction::VolumeUp:
            case PlayerAction::VolumeDown:
                break;
            default:
                return true;
        }
    }

    ExecuteAction(*action);
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = MulDiv(580, static_cast<int>(dpi_), 96);
            mmi->ptMinTrackSize.y = MulDiv(360, static_cast<int>(dpi_), 96);
            return 0;
        }

        case WM_SIZE:
            previewPopup_.Hide();
            LayoutChildren();
            return 0;

        case WM_MOVE:
            previewPopup_.Hide();
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) {
                previewPopup_.Hide();
            }
            return 0;

        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            transportBar_.SetDpi(dpi_);
            emptyAudioView_.SetDpi(dpi_);
            playlistPanel_.SetDpi(dpi_);
            previewPopup_.SetDpi(dpi_);
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(
                hwnd_, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            LayoutChildren();
            return 0;
        }

        case WM_APP_MPV_WAKEUP: {
            if (engine_.ProcessEvents(0.0)) {
                const auto& st = engine_.State();
                transportBar_.SetState(st, isFullscreen_, playlist_.IsShuffleEnabled());
                emptyAudioView_.SetState(st);
                UpdateViewportVisibility();
                UpdateWindowTitle();
                RefreshMenuState();

                if (st.loaded && st.hasVideo && !st.filePath.empty()) {
                    thumbnails_.SetMedia(st.filePath, true);
                } else if (!st.loaded) {
                    thumbnails_.ClearMedia();
                }

                // Auto-advance playlist when track finishes and Loop is off
                if (st.eofReached && !prevEofReached_ && !st.loopEnabled && playlist_.HasNext()) {
                    prevEofReached_ = true;
                    PlayNextTrack();
                    return 0;
                }
                prevEofReached_ = st.eofReached;

                if (!st.lastError.empty()) {
                    std::wstring err = st.lastError;
                    engine_.ClearLastError();
                    MessageBoxW(hwnd_, err.c_str(), L"Play Everything", MB_OK | MB_ICONWARNING);
                }
            }
            return 0;
        }

        case WM_APP_PREVIEW_READY: {
            ThumbnailFrame frame;
            if (thumbnails_.CopyLatestFrame(frame)) {
                previewPopup_.UpdateFrame(frame);
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case IDM_FILE_OPEN:         OpenFileDialog(); return 0;
                case IDM_FILE_CLOSE:
                    previewPopup_.Hide();
                    thumbnails_.ClearMedia();
                    engine_.CloseMedia();
                    UpdateViewportVisibility();
                    UpdateWindowTitle();
                    RefreshMenuState();
                    transportBar_.SetState(engine_.State(), isFullscreen_, playlist_.IsShuffleEnabled());
                    emptyAudioView_.SetState(engine_.State());
                    return 0;
                case IDM_FILE_EXIT:         PostMessageW(hwnd_, WM_CLOSE, 0, 0); return 0;
                case IDM_PLAY_TOGGLE:       ExecuteAction(PlayerAction::PlayPause); return 0;
                case IDM_PLAY_RESTART:      ExecuteAction(PlayerAction::Restart); return 0;
                case IDM_PLAY_BACK15:       ExecuteAction(PlayerAction::SeekBack15); return 0;
                case IDM_PLAY_FWD15:        ExecuteAction(PlayerAction::SeekForward15); return 0;
                case IDM_PLAY_LOOP:         ExecuteAction(PlayerAction::ToggleLoop); return 0;
                case IDM_PLAY_FULLSCREEN:   ExecuteAction(PlayerAction::ToggleFullscreen); return 0;

                case IDM_SPEED_025:         ApplySpeed(0.25); return 0;
                case IDM_SPEED_050:         ApplySpeed(0.50); return 0;
                case IDM_SPEED_075:         ApplySpeed(0.75); return 0;
                case IDM_SPEED_100:         ApplySpeed(1.00); return 0;
                case IDM_SPEED_200:         ApplySpeed(2.00); return 0;
                case IDM_SPEED_300:         ApplySpeed(3.00); return 0;
                case IDM_SPEED_CUSTOM:      OpenCustomSpeedDialog(); return 0;

                case IDM_PLAYLIST_TOGGLE:   TogglePlaylistPanel(); return 0;
                case IDM_PLAYLIST_ADD:      AddPlaylistFilesDialog(); return 0;
                case IDM_PLAYLIST_SHUFFLE:  ToggleShuffle(); return 0;
                case IDM_PLAYLIST_PREV:     PlayPrevTrack(); return 0;
                case IDM_PLAYLIST_NEXT:     PlayNextTrack(); return 0;
                case IDM_PLAYLIST_CLEAR:
                    playlist_.Clear();
                    playlistPanel_.Refresh(playlist_);
                    RefreshMenuState();
                    return 0;

                case IDM_INFO_SHOW:          OpenMediaInfoDialog(); return 0;
                case IDM_SETTINGS_KEYBINDS:  OpenKeybindsDialog(); return 0;
                case IDM_SETTINGS_UNINSTALL: LaunchUninstaller(); return 0;
                default: break;
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            bool isRepeat = (lp & (1LL << 30)) != 0;
            if (HandleKeyDown(static_cast<UINT>(wp), isRepeat)) {
                return 0;
            }
            return DefWindowProcW(hwnd_, msg, wp, lp);
        }

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                if (len > 0) {
                    std::wstring path(len, L'\0');
                    DragQueryFileW(drop, i, path.data(), len + 1);
                    files.push_back(std::move(path));
                }
            }
            DragFinish(drop);

            if (files.size() == 1) {
                OpenMediaPath(files[0], true);
            } else if (files.size() > 1) {
                int firstIdx = playlist_.AddFiles(files);
                if (firstIdx >= 0) {
                    PlayPlaylistIndex(static_cast<size_t>(firstIdx));
                    if (!playlistVisible_) {
                        playlistVisible_ = true;
                        LayoutChildren();
                        RefreshMenuState();
                    }
                }
            }
            SetForegroundWindow(hwnd_);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            ToggleFullscreen();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (delta > 0) engine_.AdjustVolume(5.0);
            else if (delta < 0) engine_.AdjustVolume(-5.0);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (isFullscreen_) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (pt.x != lastMousePt_.x || pt.y != lastMousePt_.y) {
                    lastMousePt_ = pt;
                    WakeFullscreenControls();
                }
            }
            return 0;
        }

        case WM_TIMER:
            if (wp == kTimerFullscreenHide) {
                HideFullscreenControls();
                return 0;
            }
            break;

        case WM_DESTROY:
            previewPopup_.Hide();
            thumbnails_.Stop();
            engine_.Shutdown();
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace playeverything
