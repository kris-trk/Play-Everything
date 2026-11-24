#pragma once

#include "app/AppActions.h"
#include "app/KeybindManager.h"
#include "media/MediaEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

namespace playeverything {

struct TransportCallbacks {
    std::function<void()> onPlay;
    std::function<void()> onPause;
    std::function<void()> onRestart;
    std::function<void()> onBack15;
    std::function<void()> onForward15;
    std::function<void()> onToggleLoop;
    std::function<void()> onToggleShuffle;
    std::function<void()> onToggleMute;
    std::function<void()> onToggleFullscreen;
    std::function<void()> onSpeedClick;
    std::function<void(double seconds, bool exact)> onSeek;
    std::function<void(double volume)> onVolume;
    std::function<void(double hoverSeconds, int screenX, int barTopScreenY)> onTimelineHover;
    std::function<void()> onTimelineHoverEnd;
};

class TransportBar {
public:
    static constexpr float kLogicalHeightDip = 92.0f;

    TransportBar() = default;
    ~TransportBar();

    bool Create(HWND parent, HINSTANCE hInstance,
                ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                const KeybindManager* keybinds,
                TransportCallbacks callbacks);

    HWND Handle() const { return hwnd_; }
    int PixelHeight() const;

    void SetState(const MediaState& state, bool isFullscreen, bool shuffleEnabled);
    void SetDpi(UINT dpi);
    void RefreshTooltips();

private:
    enum class HitId {
        None = 0,
        SeekBar,
        BtnSpeed,
        BtnShuffle,
        BtnRestart,
        BtnBack15,
        BtnPlay,
        BtnPause,
        BtnFwd15,
        BtnLoop,
        BtnMute,
        VolumeBar,
        BtnFullscreen,
    };

    struct LayoutRects {
        D2D1_RECT_F timeLeft{};
        D2D1_RECT_F seekTrack{};
        D2D1_RECT_F seekHit{};
        D2D1_RECT_F timeRight{};

        D2D1_RECT_F btnSpeed{};
        D2D1_RECT_F btnShuffle{};
        D2D1_RECT_F btnRestart{};
        D2D1_RECT_F btnBack15{};
        D2D1_RECT_F btnPlay{};
        D2D1_RECT_F btnPause{};
        D2D1_RECT_F btnFwd15{};
        D2D1_RECT_F btnLoop{};

        D2D1_RECT_F btnMute{};
        D2D1_RECT_F volTrack{};
        D2D1_RECT_F volHit{};
        D2D1_RECT_F btnFullscreen{};
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void RenderToHdc(HDC destHdc);
    void RenderTo(ID2D1RenderTarget* rt);

    LayoutRects ComputeLayout(float widthDip, float heightDip) const;
    HitId HitTest(float xDip, float yDip, const LayoutRects& layout) const;
    std::wstring TooltipFor(HitId id) const;
    void UpdateTooltipFor(HitId id);

    double SeekFromX(float xDip, const LayoutRects& layout) const;
    double VolumeFromX(float xDip, const LayoutRects& layout) const;

    void DrawButtonFrame(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool hovered, bool pressed, bool active);
    void DrawShuffleIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawRestartIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawSkip15Icon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool forward, ID2D1Brush* brush);
    void DrawPlayIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawPauseIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawLoopIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush);
    void DrawVolumeIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool muted, double volume, ID2D1Brush* brush);
    void DrawFullscreenIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool fullscreen, ID2D1Brush* brush);

    HWND hwnd_ = nullptr;
    HWND tooltipHwnd_ = nullptr;
    UINT dpi_ = 96;

    MediaState state_;
    bool isFullscreen_ = false;
    bool shuffleEnabled_ = false;

    HitId hovered_ = HitId::None;
    HitId pressed_ = HitId::None;
    HitId lastTooltipHit_ = HitId::None;

    bool draggingSeek_ = false;
    double dragSeekSeconds_ = 0.0;
    ULONGLONG lastSeekTick_ = 0;
    bool timelineHovered_ = false;

    bool draggingVolume_ = false;

    const KeybindManager* keybinds_ = nullptr;
    TransportCallbacks callbacks_;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRt_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> timeFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallLabelFormat_;
};

}  // namespace playeverything
