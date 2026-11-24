#include "ui/TransportBar.h"

#include "app/TimeFormat.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace playeverything {

namespace {

constexpr wchar_t kClassName[] = L"PlayEverythingTransportBar";

D2D1_COLOR_F Rgb(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
        static_cast<float>(hex & 0xFF) / 255.0f,
        alpha);
}

bool PtIn(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

}  // namespace

TransportBar::~TransportBar() {
    DiscardDeviceResources();
    if (tooltipHwnd_) {
        DestroyWindow(tooltipHwnd_);
        tooltipHwnd_ = nullptr;
    }
}

bool TransportBar::Create(HWND parent, HINSTANCE hInstance,
                          ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                          const KeybindManager* keybinds,
                          TransportCallbacks callbacks) {
    d2dFactory_ = d2dFactory;
    dwriteFactory_ = dwriteFactory;
    keybinds_ = keybinds;
    callbacks_ = std::move(callbacks);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &TransportBar::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 640, 92,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;

    // Create tooltip control
    tooltipHwnd_ = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd_, nullptr, hInstance, nullptr);
    if (tooltipHwnd_) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = hwnd_;
        ti.uId = reinterpret_cast<UINT_PTR>(hwnd_);
        ti.lpszText = const_cast<LPWSTR>(L"");
        SendMessageW(tooltipHwnd_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(tooltipHwnd_, TTM_SETDELAYTIME, TTDT_INITIAL, 350);
    }

    if (dwriteFactory_) {
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"en-us", &timeFormat_);
        if (timeFormat_) {
            timeFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            timeFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"en-us", &smallLabelFormat_);
        if (smallLabelFormat_) {
            smallLabelFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            smallLabelFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            smallLabelFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    return true;
}

int TransportBar::PixelHeight() const {
    return MulDiv(static_cast<int>(kLogicalHeightDip), static_cast<int>(dpi_), 96);
}

void TransportBar::SetState(const MediaState& state, bool isFullscreen, bool shuffleEnabled) {
    state_ = state;
    isFullscreen_ = isFullscreen;
    shuffleEnabled_ = shuffleEnabled;
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void TransportBar::SetDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    if (dcRt_) {
        dcRt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void TransportBar::RefreshTooltips() {
    lastTooltipHit_ = HitId::None;
    UpdateTooltipFor(hovered_);
}

LRESULT CALLBACK TransportBar::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TransportBar* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TransportBar*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<TransportBar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

TransportBar::LayoutRects TransportBar::ComputeLayout(float w, float h) const {
    LayoutRects L{};

    // Row 1: Seek bar
    constexpr float padX = 16.0f;
    constexpr float timeW = 58.0f;
    constexpr float row1Top = 10.0f;
    constexpr float row1H = 22.0f;
    float seekCy = row1Top + row1H * 0.5f;

    L.timeLeft = D2D1::RectF(padX, row1Top, padX + timeW, row1Top + row1H);
    L.timeRight = D2D1::RectF(w - padX - timeW, row1Top, w - padX, row1Top + row1H);

    float seekLeft = L.timeLeft.right + 10.0f;
    float seekRight = std::max(seekLeft + 20.0f, L.timeRight.left - 10.0f);
    L.seekTrack = D2D1::RectF(seekLeft, seekCy - 2.5f, seekRight, seekCy + 2.5f);
    L.seekHit = D2D1::RectF(seekLeft - 6.0f, row1Top, seekRight + 6.0f, row1Top + row1H);

    // Row 2: Transport controls
    float row2Top = 42.0f;
    float btnH = 38.0f;
    float row2Cy = row2Top + btnH * 0.5f;

    // Left cluster: Speed badge
    float speedW = 54.0f;
    L.btnSpeed = D2D1::RectF(padX, row2Top, padX + speedW, row2Top + btnH);

    // Right cluster: Mute + Volume slider + Fullscreen
    float fsW = 36.0f;
    float volW = (w >= 560.0f) ? 84.0f : 52.0f;
    float muteW = 34.0f;
    float rightGap = 8.0f;

    L.btnFullscreen = D2D1::RectF(w - padX - fsW, row2Top, w - padX, row2Top + btnH);
    float volRight = L.btnFullscreen.left - rightGap - 4.0f;
    float volLeft = volRight - volW;
    L.volTrack = D2D1::RectF(volLeft, row2Cy - 2.5f, volRight, row2Cy + 2.5f);
    L.volHit = D2D1::RectF(volLeft - 5.0f, row2Top + 6.0f, volRight + 5.0f, row2Top + btnH - 6.0f);
    L.btnMute = D2D1::RectF(volLeft - rightGap - muteW, row2Top, volLeft - rightGap, row2Top + btnH);

    // Center cluster: Shuffle, Restart, -15s, Play, Pause, +15s, Loop
    const float wShuffle = 38.0f;
    const float wRestart = 38.0f;
    const float wSkip = 50.0f;
    const float wPlayPause = 42.0f;
    const float wLoop = 38.0f;
    const float gap = 8.0f;

    float totalCenterW = wShuffle + wRestart + wSkip + wPlayPause + wPlayPause + wSkip + wLoop + gap * 6.0f;
    float minCenterLeft = L.btnSpeed.right + 12.0f;
    float maxCenterRight = L.btnMute.left - 12.0f;
    float startX = (w - totalCenterW) * 0.5f;
    if (startX + totalCenterW > maxCenterRight) {
        startX = maxCenterRight - totalCenterW;
    }
    if (startX < minCenterLeft) {
        startX = minCenterLeft;
    }

    float x = startX;
    L.btnShuffle = D2D1::RectF(x, row2Top, x + wShuffle, row2Top + btnH);
    x += wShuffle + gap;
    L.btnRestart = D2D1::RectF(x, row2Top, x + wRestart, row2Top + btnH);
    x += wRestart + gap;
    L.btnBack15 = D2D1::RectF(x, row2Top, x + wSkip, row2Top + btnH);
    x += wSkip + gap;
    L.btnPlay = D2D1::RectF(x, row2Top, x + wPlayPause, row2Top + btnH);
    x += wPlayPause + gap;
    L.btnPause = D2D1::RectF(x, row2Top, x + wPlayPause, row2Top + btnH);
    x += wPlayPause + gap;
    L.btnFwd15 = D2D1::RectF(x, row2Top, x + wSkip, row2Top + btnH);
    x += wSkip + gap;
    L.btnLoop = D2D1::RectF(x, row2Top, x + wLoop, row2Top + btnH);

    (void)h;
    return L;
}

TransportBar::HitId TransportBar::HitTest(float x, float y, const LayoutRects& L) const {
    if (PtIn(x, y, L.seekHit)) return HitId::SeekBar;
    if (PtIn(x, y, L.btnSpeed)) return HitId::BtnSpeed;
    if (PtIn(x, y, L.btnShuffle)) return HitId::BtnShuffle;
    if (PtIn(x, y, L.btnRestart)) return HitId::BtnRestart;
    if (PtIn(x, y, L.btnBack15)) return HitId::BtnBack15;
    if (PtIn(x, y, L.btnPlay)) return HitId::BtnPlay;
    if (PtIn(x, y, L.btnPause)) return HitId::BtnPause;
    if (PtIn(x, y, L.btnFwd15)) return HitId::BtnFwd15;
    if (PtIn(x, y, L.btnLoop)) return HitId::BtnLoop;
    if (PtIn(x, y, L.btnMute)) return HitId::BtnMute;
    if (PtIn(x, y, L.volHit)) return HitId::VolumeBar;
    if (PtIn(x, y, L.btnFullscreen)) return HitId::BtnFullscreen;
    return HitId::None;
}

std::wstring TransportBar::TooltipFor(HitId id) const {
    auto withKey = [this](const std::wstring& label, PlayerAction action) -> std::wstring {
        if (!keybinds_) return label;
        std::wstring sc = keybinds_->GetPrimaryShortcut(action);
        if (sc.empty()) return label;
        return label + L" (" + sc + L")";
    };

    switch (id) {
        case HitId::BtnSpeed: {
            wchar_t sb[48]{};
            std::swprintf(sb, std::size(sb), L"Playback Speed (%.2gx)", state_.speed);
            return sb;
        }
        case HitId::BtnShuffle:    return withKey(shuffleEnabled_ ? L"Shuffle: On" : L"Shuffle: Off", PlayerAction::ToggleShuffle);
        case HitId::BtnRestart:    return withKey(L"Restart", PlayerAction::Restart);
        case HitId::BtnBack15:     return withKey(L"Backward 15s", PlayerAction::SeekBack15);
        case HitId::BtnPlay: {
            std::wstring sc = keybinds_ ? keybinds_->GetPrimaryShortcut(PlayerAction::Play) : L"";
            if (sc.empty() && keybinds_) sc = keybinds_->GetPrimaryShortcut(PlayerAction::PlayPause);
            return sc.empty() ? L"Play" : (L"Play (" + sc + L")");
        }
        case HitId::BtnPause: {
            std::wstring sc = keybinds_ ? keybinds_->GetPrimaryShortcut(PlayerAction::Pause) : L"";
            if (sc.empty() && keybinds_) sc = keybinds_->GetPrimaryShortcut(PlayerAction::PlayPause);
            return sc.empty() ? L"Pause" : (L"Pause (" + sc + L")");
        }
        case HitId::BtnFwd15:      return withKey(L"Forward 15s", PlayerAction::SeekForward15);
        case HitId::BtnLoop:       return withKey(state_.loopEnabled ? L"Loop: On" : L"Loop: Off", PlayerAction::ToggleLoop);
        case HitId::BtnMute:       return withKey(state_.muted ? L"Unmute" : L"Mute", PlayerAction::ToggleMute);
        case HitId::BtnFullscreen: return withKey(isFullscreen_ ? L"Exit Fullscreen" : L"Fullscreen", PlayerAction::ToggleFullscreen);
        case HitId::VolumeBar:     return L"Volume: " + std::to_wstring(static_cast<int>(std::round(state_.volume))) + L"%";
        case HitId::SeekBar:       return L"Seek";
        default:                   return {};
    }
}

void TransportBar::UpdateTooltipFor(HitId id) {
    if (!tooltipHwnd_ || id == lastTooltipHit_) return;
    lastTooltipHit_ = id;

    std::wstring tip = TooltipFor(id);
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd_;
    ti.uId = reinterpret_cast<UINT_PTR>(hwnd_);
    ti.lpszText = const_cast<LPWSTR>(tip.c_str());
    SendMessageW(tooltipHwnd_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    if (tip.empty()) {
        SendMessageW(tooltipHwnd_, TTM_POP, 0, 0);
    }
}

double TransportBar::SeekFromX(float xDip, const LayoutRects& L) const {
    float span = std::max(1.0f, L.seekTrack.right - L.seekTrack.left);
    double frac = std::clamp(static_cast<double>(xDip - L.seekTrack.left) / static_cast<double>(span), 0.0, 1.0);
    return frac * std::max(0.0, state_.duration);
}

double TransportBar::VolumeFromX(float xDip, const LayoutRects& L) const {
    float span = std::max(1.0f, L.volTrack.right - L.volTrack.left);
    double frac = std::clamp(static_cast<double>(xDip - L.volTrack.left) / static_cast<double>(span), 0.0, 1.0);
    return frac * 100.0;
}

LRESULT TransportBar::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            RenderToHdc(hdc);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_PRINTCLIENT: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            if (hdc) {
                RenderToHdc(hdc);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            float scale = static_cast<float>(dpi_) / 96.0f;
            float w = static_cast<float>(rc.right - rc.left) / scale;
            float h = static_cast<float>(rc.bottom - rc.top) / scale;
            float mx = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            float my = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
            auto layout = ComputeLayout(w, h);

            auto notifyTimelineHover = [&](double sec) {
                if (!callbacks_.onTimelineHover || !state_.loaded || state_.duration <= 0.0) return;
                float clampedX = std::clamp(mx, layout.seekTrack.left, layout.seekTrack.right);
                POINT pt{static_cast<LONG>(std::round(clampedX * scale)), 0};
                ClientToScreen(hwnd_, &pt);
                timelineHovered_ = true;
                callbacks_.onTimelineHover(sec, pt.x, pt.y);
            };

            if (draggingSeek_) {
                dragSeekSeconds_ = SeekFromX(mx, layout);
                notifyTimelineHover(dragSeekSeconds_);
                ULONGLONG now = GetTickCount64();
                if (now - lastSeekTick_ >= 50 && callbacks_.onSeek) {
                    callbacks_.onSeek(dragSeekSeconds_, false);
                    lastSeekTick_ = now;
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (draggingVolume_) {
                double vol = VolumeFromX(mx, layout);
                state_.volume = vol;
                if (callbacks_.onVolume) callbacks_.onVolume(vol);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }

            HitId hit = HitTest(mx, my, layout);
            if (hit == HitId::SeekBar && state_.loaded && state_.duration > 0.0) {
                notifyTimelineHover(SeekFromX(mx, layout));
            } else if (timelineHovered_) {
                timelineHovered_ = false;
                if (callbacks_.onTimelineHoverEnd) callbacks_.onTimelineHoverEnd();
            }

            if (hit != hovered_) {
                hovered_ = hit;
                UpdateTooltipFor(hit);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, hit != HitId::None ? IDC_HAND : IDC_ARROW));

            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);

            // Keep fullscreen controls awake while hovering transport bar
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &pt);
            ScreenToClient(GetParent(hwnd_), &pt);
            PostMessageW(GetParent(hwnd_), WM_MOUSEMOVE, wp, MAKELPARAM(pt.x, pt.y));
            return 0;
        }

        case WM_MOUSELEAVE:
            if (!draggingSeek_ && !draggingVolume_) {
                hovered_ = HitId::None;
                pressed_ = HitId::None;
                if (timelineHovered_) {
                    timelineHovered_ = false;
                    if (callbacks_.onTimelineHoverEnd) callbacks_.onTimelineHoverEnd();
                }
                UpdateTooltipFor(HitId::None);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(GetParent(hwnd_));
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            float scale = static_cast<float>(dpi_) / 96.0f;
            float w = static_cast<float>(rc.right - rc.left) / scale;
            float h = static_cast<float>(rc.bottom - rc.top) / scale;
            float mx = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            float my = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
            auto layout = ComputeLayout(w, h);

            HitId hit = HitTest(mx, my, layout);
            pressed_ = hit;
            SetCapture(hwnd_);

            if (hit == HitId::SeekBar && state_.loaded && state_.duration > 0.0) {
                draggingSeek_ = true;
                dragSeekSeconds_ = SeekFromX(mx, layout);
                lastSeekTick_ = GetTickCount64();
                if (callbacks_.onSeek) callbacks_.onSeek(dragSeekSeconds_, false);
                if (callbacks_.onTimelineHover) {
                    float clampedX = std::clamp(mx, layout.seekTrack.left, layout.seekTrack.right);
                    POINT pt{static_cast<LONG>(std::round(clampedX * scale)), 0};
                    ClientToScreen(hwnd_, &pt);
                    timelineHovered_ = true;
                    callbacks_.onTimelineHover(dragSeekSeconds_, pt.x, pt.y);
                }
            } else if (hit == HitId::VolumeBar) {
                draggingVolume_ = true;
                double vol = VolumeFromX(mx, layout);
                state_.volume = vol;
                if (callbacks_.onVolume) callbacks_.onVolume(vol);
            }

            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            // Snapshot interaction state BEFORE ReleaseCapture(), because ReleaseCapture()
            // synchronously fires WM_CAPTURECHANGED which clears pressed_/dragging flags.
            bool wasDragSeek = draggingSeek_;
            bool wasDragVol = draggingVolume_;
            HitId wasPressed = pressed_;

            draggingSeek_ = false;
            draggingVolume_ = false;
            pressed_ = HitId::None;

            if (GetCapture() == hwnd_) {
                ReleaseCapture();
            }

            RECT rc{};
            GetClientRect(hwnd_, &rc);
            float scale = static_cast<float>(dpi_) / 96.0f;
            float w = static_cast<float>(rc.right - rc.left) / scale;
            float h = static_cast<float>(rc.bottom - rc.top) / scale;
            float mx = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            float my = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
            auto layout = ComputeLayout(w, h);

            if (wasDragSeek) {
                dragSeekSeconds_ = SeekFromX(mx, layout);
                if (callbacks_.onSeek) callbacks_.onSeek(dragSeekSeconds_, true);
                if (HitTest(mx, my, layout) != HitId::SeekBar && timelineHovered_) {
                    timelineHovered_ = false;
                    if (callbacks_.onTimelineHoverEnd) callbacks_.onTimelineHoverEnd();
                }
            } else if (wasDragVol) {
                // Volume already updated
            } else if (wasPressed != HitId::None) {
                HitId hit = HitTest(mx, my, layout);
                if (hit == wasPressed) {
                    switch (hit) {
                        case HitId::BtnSpeed:      if (callbacks_.onSpeedClick) callbacks_.onSpeedClick(); break;
                        case HitId::BtnShuffle:    if (callbacks_.onToggleShuffle) callbacks_.onToggleShuffle(); break;
                        case HitId::BtnRestart:    if (callbacks_.onRestart) callbacks_.onRestart(); break;
                        case HitId::BtnBack15:     if (callbacks_.onBack15) callbacks_.onBack15(); break;
                        case HitId::BtnPlay:       if (callbacks_.onPlay) callbacks_.onPlay(); break;
                        case HitId::BtnPause:      if (callbacks_.onPause) callbacks_.onPause(); break;
                        case HitId::BtnFwd15:      if (callbacks_.onForward15) callbacks_.onForward15(); break;
                        case HitId::BtnLoop:       if (callbacks_.onToggleLoop) callbacks_.onToggleLoop(); break;
                        case HitId::BtnMute:       if (callbacks_.onToggleMute) callbacks_.onToggleMute(); break;
                        case HitId::BtnFullscreen: if (callbacks_.onToggleFullscreen) callbacks_.onToggleFullscreen(); break;
                        default: break;
                    }
                }
            }

            lastTooltipHit_ = HitId::None;
            UpdateTooltipFor(hovered_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }

        case WM_CAPTURECHANGED:
            draggingSeek_ = false;
            draggingVolume_ = false;
            pressed_ = HitId::None;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;

        case WM_MOUSEWHEEL:
            return SendMessageW(GetParent(hwnd_), msg, wp, lp);

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void TransportBar::EnsureDeviceResources() {
    if (dcRt_ || !d2dFactory_) return;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_),
        static_cast<float>(dpi_),
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    HRESULT hr = d2dFactory_->CreateDCRenderTarget(&props, &dcRt_);
    if (FAILED(hr)) {
        props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        d2dFactory_->CreateDCRenderTarget(&props, &dcRt_);
    }
}

void TransportBar::DiscardDeviceResources() {
    dcRt_.Reset();
}

void TransportBar::RenderToHdc(HDC destHdc) {
    if (!destHdc || !hwnd_) return;
    EnsureDeviceResources();
    if (!dcRt_) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC memDc = CreateCompatibleDC(destHdc);
    HBITMAP dib = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memDc || !dib) {
        if (dib) DeleteObject(dib);
        if (memDc) DeleteDC(memDc);
        return;
    }

    HGDIOBJ oldBmp = SelectObject(memDc, dib);
    RECT drawRc{0, 0, w, h};
    if (SUCCEEDED(dcRt_->BindDC(memDc, &drawRc))) {
        RenderTo(dcRt_.Get());
        BitBlt(destHdc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
    }

    SelectObject(memDc, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDc);
}

void TransportBar::DrawButtonFrame(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool hovered, bool pressed, bool active) {
    if (!hovered && !pressed && !active) return;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;

    if (active) {
        rt->CreateSolidColorBrush(Rgb(0x3B82F6, pressed ? 0.34f : (hovered ? 0.28f : 0.20f)), &fill);
        rt->CreateSolidColorBrush(Rgb(0x60A5FA, 0.55f), &border);
    } else if (pressed) {
        rt->CreateSolidColorBrush(Rgb(0x2D3446), &fill);
    } else {
        rt->CreateSolidColorBrush(Rgb(0x232836), &fill);
    }

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, 8.0f, 8.0f);
    if (fill) rt->FillRoundedRectangle(rr, fill.Get());
    if (border) rt->DrawRoundedRectangle(rr, border.Get(), 1.0f);
}

void TransportBar::DrawShuffleIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    // Top-left to bottom-right crossing path
    rt->DrawLine(D2D1::Point2F(cx - 7.5f, cy - 4.5f), D2D1::Point2F(cx - 3.0f, cy - 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx - 3.0f, cy - 4.5f), D2D1::Point2F(cx + 3.0f, cy + 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 3.0f, cy + 4.5f), D2D1::Point2F(cx + 7.0f, cy + 4.5f), brush, 1.8f);

    // Bottom-left to top-right crossing path
    rt->DrawLine(D2D1::Point2F(cx - 7.5f, cy + 4.5f), D2D1::Point2F(cx - 3.0f, cy + 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 4.5f), D2D1::Point2F(cx + 3.0f, cy - 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 3.0f, cy - 4.5f), D2D1::Point2F(cx + 7.0f, cy - 4.5f), brush, 1.8f);

    // Top-right arrowhead
    rt->DrawLine(D2D1::Point2F(cx + 4.5f, cy - 7.2f), D2D1::Point2F(cx + 7.5f, cy - 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 4.5f, cy - 1.8f), D2D1::Point2F(cx + 7.5f, cy - 4.5f), brush, 1.8f);

    // Bottom-right arrowhead
    rt->DrawLine(D2D1::Point2F(cx + 4.5f, cy + 1.8f), D2D1::Point2F(cx + 7.5f, cy + 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 4.5f, cy + 7.2f), D2D1::Point2F(cx + 7.5f, cy + 4.5f), brush, 1.8f);
}

void TransportBar::DrawRestartIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    // Vertical bar on left
    rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx - 7.5f, cy - 6.5f, cx - 5.2f, cy + 6.5f), 1.0f, 1.0f),
        brush);

    // Left-pointing triangle
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> tri;
    if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&tri))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(tri->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - 4.0f, cy), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx + 7.0f, cy - 6.5f));
            sink->AddLine(D2D1::Point2F(cx + 7.0f, cy + 6.5f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(tri.Get(), brush);
        }
    }
}

void TransportBar::DrawSkip15Icon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool forward, ID2D1Brush* brush) {
    if (!smallLabelFormat_) return;
    std::wstring txt = forward ? L"+15s" : L"-15s";
    rt->DrawTextW(txt.c_str(), static_cast<UINT32>(txt.size()), smallLabelFormat_.Get(), r, brush);
}

void TransportBar::DrawPlayIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> tri;
    if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&tri))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(tri->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - 5.0f, cy - 7.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx + 8.0f, cy));
            sink->AddLine(D2D1::Point2F(cx - 5.0f, cy + 7.5f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(tri.Get(), brush);
        }
    }
}

void TransportBar::DrawPauseIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx - 6.0f, cy - 7.0f, cx - 2.0f, cy + 7.0f), 1.2f, 1.2f),
        brush);
    rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(cx + 2.0f, cy - 7.0f, cx + 6.0f, cy + 7.0f), 1.2f, 1.2f),
        brush);
}

void TransportBar::DrawLoopIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    // Top path going right
    rt->DrawLine(D2D1::Point2F(cx - 6.0f, cy - 4.5f), D2D1::Point2F(cx + 5.5f, cy - 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx - 6.0f, cy - 4.5f), D2D1::Point2F(cx - 6.0f, cy - 1.0f), brush, 1.8f);
    // Top arrowhead
    rt->DrawLine(D2D1::Point2F(cx + 3.0f, cy - 7.5f), D2D1::Point2F(cx + 6.5f, cy - 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 3.0f, cy - 1.5f), D2D1::Point2F(cx + 6.5f, cy - 4.5f), brush, 1.8f);

    // Bottom path going left
    rt->DrawLine(D2D1::Point2F(cx + 6.0f, cy + 4.5f), D2D1::Point2F(cx - 5.5f, cy + 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx + 6.0f, cy + 4.5f), D2D1::Point2F(cx + 6.0f, cy + 1.0f), brush, 1.8f);
    // Bottom arrowhead
    rt->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 7.5f), D2D1::Point2F(cx - 6.5f, cy + 4.5f), brush, 1.8f);
    rt->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 1.5f), D2D1::Point2F(cx - 6.5f, cy + 4.5f), brush, 1.8f);
}

void TransportBar::DrawVolumeIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool muted, double volume, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    // Speaker cone
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> spk;
    if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&spk))) {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(spk->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(cx - 8.0f, cy - 3.0f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(cx - 4.0f, cy - 3.0f));
            sink->AddLine(D2D1::Point2F(cx + 0.5f, cy - 7.0f));
            sink->AddLine(D2D1::Point2F(cx + 0.5f, cy + 7.0f));
            sink->AddLine(D2D1::Point2F(cx - 4.0f, cy + 3.0f));
            sink->AddLine(D2D1::Point2F(cx - 8.0f, cy + 3.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            rt->FillGeometry(spk.Get(), brush);
        }
    }

    if (muted || volume <= 0.5) {
        // Cross mark
        rt->DrawLine(D2D1::Point2F(cx + 3.5f, cy - 3.5f), D2D1::Point2F(cx + 9.0f, cy + 3.5f), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + 9.0f, cy - 3.5f), D2D1::Point2F(cx + 3.5f, cy + 3.5f), brush, 1.8f);
    } else {
        // Sound waves
        rt->DrawLine(D2D1::Point2F(cx + 3.5f, cy - 3.0f), D2D1::Point2F(cx + 4.8f, cy), brush, 1.7f);
        rt->DrawLine(D2D1::Point2F(cx + 4.8f, cy), D2D1::Point2F(cx + 3.5f, cy + 3.0f), brush, 1.7f);
        if (volume > 45.0) {
            rt->DrawLine(D2D1::Point2F(cx + 6.5f, cy - 5.5f), D2D1::Point2F(cx + 8.5f, cy), brush, 1.7f);
            rt->DrawLine(D2D1::Point2F(cx + 8.5f, cy), D2D1::Point2F(cx + 6.5f, cy + 5.5f), brush, 1.7f);
        }
    }
}

void TransportBar::DrawFullscreenIcon(ID2D1RenderTarget* rt, const D2D1_RECT_F& r, bool fullscreen, ID2D1Brush* brush) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;
    float d = 6.5f;
    float arm = 3.8f;

    if (!fullscreen) {
        // Outward corners
        rt->DrawLine(D2D1::Point2F(cx - d, cy - d + arm), D2D1::Point2F(cx - d, cy - d), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx - d, cy - d), D2D1::Point2F(cx - d + arm, cy - d), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx + d - arm, cy - d), D2D1::Point2F(cx + d, cy - d), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + d, cy - d), D2D1::Point2F(cx + d, cy - d + arm), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx - d, cy + d - arm), D2D1::Point2F(cx - d, cy + d), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx - d, cy + d), D2D1::Point2F(cx - d + arm, cy + d), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx + d - arm, cy + d), D2D1::Point2F(cx + d, cy + d), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + d, cy + d), D2D1::Point2F(cx + d, cy + d - arm), brush, 1.8f);
    } else {
        // Inward corners
        float i = 2.5f;
        rt->DrawLine(D2D1::Point2F(cx - i, cy - d), D2D1::Point2F(cx - i, cy - i), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx - d, cy - i), D2D1::Point2F(cx - i, cy - i), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx + i, cy - d), D2D1::Point2F(cx + i, cy - i), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + d, cy - i), D2D1::Point2F(cx + i, cy - i), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx - i, cy + d), D2D1::Point2F(cx - i, cy + i), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx - d, cy + i), D2D1::Point2F(cx - i, cy + i), brush, 1.8f);

        rt->DrawLine(D2D1::Point2F(cx + i, cy + d), D2D1::Point2F(cx + i, cy + i), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + d, cy + i), D2D1::Point2F(cx + i, cy + i), brush, 1.8f);
    }
}

void TransportBar::RenderTo(ID2D1RenderTarget* rt) {
    if (!rt) return;

    rt->BeginDraw();
    rt->Clear(Rgb(0x161922));

    D2D1_SIZE_F sz = rt->GetSize();
    auto L = ComputeLayout(sz.width, sz.height);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBright;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textPrimary;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textSecondary;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> disabledBrush;

    rt->CreateSolidColorBrush(Rgb(0x262B38), &borderBrush);
    rt->CreateSolidColorBrush(Rgb(0x2B3142), &trackBg);
    rt->CreateSolidColorBrush(Rgb(0x3B82F6), &accentBrush);
    rt->CreateSolidColorBrush(Rgb(0x60A5FA), &accentBright);
    rt->CreateSolidColorBrush(Rgb(0xF2F4F8), &textPrimary);
    rt->CreateSolidColorBrush(Rgb(0x98A2B3), &textSecondary);
    rt->CreateSolidColorBrush(Rgb(0x525B70), &disabledBrush);

    // Subtle top divider
    rt->DrawLine(D2D1::Point2F(0.0f, 0.5f), D2D1::Point2F(sz.width, 0.5f), borderBrush.Get(), 1.0f);

    // Seek bar & timestamps
    double shownPos = draggingSeek_ ? dragSeekSeconds_ : state_.timePos;
    double dur = state_.duration;
    bool forceHours = dur >= 3600.0;

    if (timeFormat_) {
        std::wstring leftTxt = FormatTimestamp(state_.loaded ? shownPos : 0.0, forceHours);
        std::wstring rightTxt = FormatTimestamp(state_.loaded ? dur : 0.0, forceHours);
        timeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        rt->DrawTextW(leftTxt.c_str(), static_cast<UINT32>(leftTxt.size()), timeFormat_.Get(), L.timeLeft, textSecondary.Get());
        timeFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        rt->DrawTextW(rightTxt.c_str(), static_cast<UINT32>(rightTxt.size()), timeFormat_.Get(), L.timeRight, textSecondary.Get());
    }

    rt->FillRoundedRectangle(D2D1::RoundedRect(L.seekTrack, 2.5f, 2.5f), trackBg.Get());

    double frac = (state_.loaded && dur > 0.0) ? std::clamp(shownPos / dur, 0.0, 1.0) : 0.0;
    float fillX = L.seekTrack.left + static_cast<float>(frac) * (L.seekTrack.right - L.seekTrack.left);
    if (fillX > L.seekTrack.left + 0.5f) {
        D2D1_RECT_F filled = L.seekTrack;
        filled.right = fillX;
        rt->FillRoundedRectangle(D2D1::RoundedRect(filled, 2.5f, 2.5f), accentBrush.Get());
    }

    if (state_.loaded && dur > 0.0) {
        float thumbR = (draggingSeek_ || hovered_ == HitId::SeekBar) ? 7.0f : 5.0f;
        float cy = (L.seekTrack.top + L.seekTrack.bottom) * 0.5f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, cy), thumbR, thumbR), textPrimary.Get());
    }

    // Left cluster: Speed badge button
    bool customSpeed = std::abs(state_.speed - 1.0) > 0.005;
    DrawButtonFrame(rt, L.btnSpeed, hovered_ == HitId::BtnSpeed, pressed_ == HitId::BtnSpeed, customSpeed);
    if (smallLabelFormat_) {
        wchar_t spdTxt[24]{};
        if (std::abs(state_.speed - std::round(state_.speed)) < 0.005) {
            std::swprintf(spdTxt, std::size(spdTxt), L"%.0fx", state_.speed);
        } else {
            std::swprintf(spdTxt, std::size(spdTxt), L"%.2gx", state_.speed);
        }
        std::wstring s(spdTxt);
        rt->DrawTextW(s.c_str(), static_cast<UINT32>(s.size()), smallLabelFormat_.Get(), L.btnSpeed,
                      customSpeed ? accentBright.Get() : textSecondary.Get());
    }

    // Center transport buttons
    bool isPlaying = state_.loaded && !state_.paused && !state_.eofReached;
    bool isPaused = state_.loaded && (state_.paused || state_.eofReached);
    ID2D1Brush* mediaIconBrush = state_.loaded ? textPrimary.Get() : disabledBrush.Get();

    DrawButtonFrame(rt, L.btnShuffle, hovered_ == HitId::BtnShuffle, pressed_ == HitId::BtnShuffle, shuffleEnabled_);
    DrawShuffleIcon(rt, L.btnShuffle, shuffleEnabled_ ? accentBright.Get() : textPrimary.Get());

    DrawButtonFrame(rt, L.btnRestart, hovered_ == HitId::BtnRestart, pressed_ == HitId::BtnRestart, false);
    DrawRestartIcon(rt, L.btnRestart, mediaIconBrush);

    DrawButtonFrame(rt, L.btnBack15, hovered_ == HitId::BtnBack15, pressed_ == HitId::BtnBack15, false);
    DrawSkip15Icon(rt, L.btnBack15, false, mediaIconBrush);

    DrawButtonFrame(rt, L.btnPlay, hovered_ == HitId::BtnPlay, pressed_ == HitId::BtnPlay, isPlaying);
    DrawPlayIcon(rt, L.btnPlay, isPlaying ? accentBright.Get() : mediaIconBrush);

    DrawButtonFrame(rt, L.btnPause, hovered_ == HitId::BtnPause, pressed_ == HitId::BtnPause, isPaused);
    DrawPauseIcon(rt, L.btnPause, isPaused ? accentBright.Get() : mediaIconBrush);

    DrawButtonFrame(rt, L.btnFwd15, hovered_ == HitId::BtnFwd15, pressed_ == HitId::BtnFwd15, false);
    DrawSkip15Icon(rt, L.btnFwd15, true, mediaIconBrush);

    DrawButtonFrame(rt, L.btnLoop, hovered_ == HitId::BtnLoop, pressed_ == HitId::BtnLoop, state_.loopEnabled);
    DrawLoopIcon(rt, L.btnLoop, state_.loopEnabled ? accentBright.Get() : textPrimary.Get());

    // Right cluster: Mute, Volume, Fullscreen
    DrawButtonFrame(rt, L.btnMute, hovered_ == HitId::BtnMute, pressed_ == HitId::BtnMute, state_.muted);
    DrawVolumeIcon(rt, L.btnMute, state_.muted, state_.volume, state_.muted ? accentBright.Get() : textPrimary.Get());

    rt->FillRoundedRectangle(D2D1::RoundedRect(L.volTrack, 2.5f, 2.5f), trackBg.Get());
    double shownVol = state_.muted ? 0.0 : std::clamp(state_.volume / 100.0, 0.0, 1.0);
    float volX = L.volTrack.left + static_cast<float>(shownVol) * (L.volTrack.right - L.volTrack.left);
    if (volX > L.volTrack.left + 0.5f) {
        D2D1_RECT_F vf = L.volTrack;
        vf.right = volX;
        rt->FillRoundedRectangle(D2D1::RoundedRect(vf, 2.5f, 2.5f), accentBrush.Get());
    }
    {
        float volR = (draggingVolume_ || hovered_ == HitId::VolumeBar) ? 6.0f : 4.5f;
        float cy = (L.volTrack.top + L.volTrack.bottom) * 0.5f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(volX, cy), volR, volR), textPrimary.Get());
    }

    DrawButtonFrame(rt, L.btnFullscreen, hovered_ == HitId::BtnFullscreen, pressed_ == HitId::BtnFullscreen, isFullscreen_);
    DrawFullscreenIcon(rt, L.btnFullscreen, isFullscreen_, isFullscreen_ ? accentBright.Get() : textPrimary.Get());

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

}  // namespace playeverything
