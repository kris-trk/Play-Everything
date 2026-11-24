#pragma once

#include "media/ThumbnailProvider.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace playeverything {

class TimelinePreviewPopup {
public:
    TimelinePreviewPopup() = default;
    ~TimelinePreviewPopup();

    bool Create(HWND owner, HINSTANCE hInstance);
    void SetDpi(UINT dpi);

    void ShowAt(int anchorScreenX, int barTopScreenY, double seconds, double duration,
                bool hasVideo, const RECT& ownerScreenBounds);
    void Hide();
    void UpdateFrame(const ThumbnailFrame& frame);

    bool IsVisible() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC hdc);
    void RecreateFont();

    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    bool hasVideo_ = false;
    double hoverSeconds_ = 0.0;
    double duration_ = 0.0;
    ThumbnailFrame frame_;
};

}  // namespace playeverything
