#pragma once

#include "media/MediaEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>

namespace playeverything {

class EmptyAudioView {
public:
    EmptyAudioView() = default;
    ~EmptyAudioView();

    bool Create(HWND parent, HINSTANCE hInstance,
                ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                std::function<void()> onOpenFile,
                std::function<void()> onDoubleClick);

    HWND Handle() const { return hwnd_; }
    void SetState(const MediaState& state);
    void SetDpi(UINT dpi);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void RenderToHdc(HDC destHdc);
    void RenderTo(ID2D1RenderTarget* target);
    float Scale(float dip) const { return dip * (static_cast<float>(dpi_) / 96.0f); }
    D2D1_RECT_F GetOpenButtonRect(float widthDip, float heightDip) const;

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    MediaState state_;
    bool hoverOpenBtn_ = false;
    bool pressOpenBtn_ = false;

    std::function<void()> onOpenFile_;
    std::function<void()> onDoubleClick_;

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRt_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> subFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> btnFormat_;
};

}  // namespace playeverything
