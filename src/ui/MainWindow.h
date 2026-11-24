#pragma once

#include "app/AppActions.h"
#include "app/KeybindManager.h"
#include "app/PlaylistManager.h"
#include "media/MediaEngine.h"
#include "media/ThumbnailProvider.h"
#include "ui/EmptyAudioView.h"
#include "ui/PlaylistPanel.h"
#include "ui/TimelinePreviewPopup.h"
#include "ui/TransportBar.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace playeverything {

class MainWindow {
public:
    MainWindow() = default;
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow, const std::wstring& initialFile);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void BuildMenu();
    void RefreshMenuState();
    void LayoutChildren();
    void UpdateWindowTitle();
    void UpdateViewportVisibility();

    void ExecuteAction(PlayerAction action);
    std::vector<std::wstring> ShowMediaFilePicker(const std::wstring& title, bool multiSelect);
    void OpenFileDialog();
    void AddPlaylistFilesDialog();
    void OpenMediaPath(const std::wstring& path, bool addToPlaylist = true);
    void OpenKeybindsDialog();
    void LaunchUninstaller();
    void OpenMediaInfoDialog();
    void OpenCustomSpeedDialog();
    void ShowSpeedPopupMenu();
    void ApplySpeed(double speed);

    void TogglePlaylistPanel();
    void ToggleShuffle();
    void PlayPlaylistIndex(size_t index);
    void PlayNextTrack();
    void PlayPrevTrack();

    void ToggleFullscreen();
    void SetFullscreen(bool enable);
    void WakeFullscreenControls();
    void HideFullscreenControls();

    bool HandleKeyDown(UINT vk, bool isRepeat);
    std::wstring KeybindsFilePath() const;

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND videoHostHwnd_ = nullptr;
    HMENU menuBar_ = nullptr;
    HMENU playbackMenu_ = nullptr;
    HMENU speedMenu_ = nullptr;
    HMENU playlistMenu_ = nullptr;
    HICON appIconBig_ = nullptr;
    HICON appIconSmall_ = nullptr;
    UINT dpi_ = 96;

    bool isFullscreen_ = false;
    bool playlistVisible_ = false;
    bool controlsHidden_ = false;
    bool cursorHidden_ = false;
    bool prevEofReached_ = false;
    WINDOWPLACEMENT savedPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD savedStyle_ = 0;
    POINT lastMousePt_{-1, -1};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;

    KeybindManager keybinds_;
    PlaylistManager playlist_;
    MediaEngine engine_;
    ThumbnailProvider thumbnails_;
    TransportBar transportBar_;
    EmptyAudioView emptyAudioView_;
    PlaylistPanel playlistPanel_;
    TimelinePreviewPopup previewPopup_;
};

}  // namespace playeverything
