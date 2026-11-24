#pragma once

#include "app/PlaylistManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <functional>

namespace playeverything {

struct PlaylistPanelCallbacks {
    std::function<void(size_t index)> onPlayIndex;
    std::function<void()> onAddFiles;
    std::function<void(size_t index)> onRemoveIndex;
    std::function<void(size_t index)> onMoveUp;
    std::function<void(size_t index)> onMoveDown;
    std::function<void()> onClear;
    std::function<void()> onClose;
};

class PlaylistPanel {
public:
    static constexpr int kLogicalWidthDip = 264;

    PlaylistPanel() = default;
    ~PlaylistPanel();

    bool Create(HWND parent, HINSTANCE hInstance, PlaylistPanelCallbacks callbacks);
    HWND Handle() const { return hwnd_; }
    int PixelWidth() const;

    void SetDpi(UINT dpi);
    void Refresh(const PlaylistManager& playlist);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void RecreateFonts();
    void Layout();
    int SelectedIndex() const;

    HWND hwnd_ = nullptr;
    HWND lblTitle_ = nullptr;
    HWND btnClose_ = nullptr;
    HWND listBox_ = nullptr;
    HWND btnAdd_ = nullptr;
    HWND btnUp_ = nullptr;
    HWND btnDown_ = nullptr;
    HWND btnRemove_ = nullptr;
    HWND btnClear_ = nullptr;

    HFONT font_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBRUSH bgBrush_ = nullptr;
    HBRUSH listBrush_ = nullptr;

    UINT dpi_ = 96;
    PlaylistPanelCallbacks callbacks_;
};

}  // namespace playeverything
