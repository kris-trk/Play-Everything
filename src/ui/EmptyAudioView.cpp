#include "ui/EmptyAudioView.h"

#include <windowsx.h>
#include <algorithm>
#include <cstdio>

namespace playeverything {

namespace {

constexpr wchar_t kClassName[] = L"PlayEverythingEmptyAudioView";

D2D1_COLOR_F Rgb(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
        static_cast<float>(hex & 0xFF) / 255.0f,
        alpha);
}

bool PointInRect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

std::wstring UpperAscii(std::wstring s) {
    for (wchar_t& c : s) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return s;
}

}  // namespace

EmptyAudioView::~EmptyAudioView() {
    DiscardDeviceResources();
}

bool EmptyAudioView::Create(HWND parent, HINSTANCE hInstance,
                            ID2D1Factory* d2dFactory, IDWriteFactory* dwriteFactory,
                            std::function<void()> onOpenFile,
                            std::function<void()> onDoubleClick) {
    d2dFactory_ = d2dFactory;
    dwriteFactory_ = dwriteFactory;
    onOpenFile_ = std::move(onOpenFile);
    onDoubleClick_ = std::move(onDoubleClick);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &EmptyAudioView::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        parent, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = 96;

    if (dwriteFactory_) {
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            22.0f, L"en-us", &titleFormat_);
        if (titleFormat_) {
            titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(dwriteFactory_->CreateEllipsisTrimmingSign(titleFormat_.Get(), &ellipsis))) {
                titleFormat_->SetTrimming(&trim, ellipsis.Get());
            }
            titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"en-us", &subFormat_);
        if (subFormat_) {
            subFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            subFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        dwriteFactory_->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"en-us", &btnFormat_);
        if (btnFormat_) {
            btnFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            btnFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    return true;
}

void EmptyAudioView::SetState(const MediaState& state) {
    state_ = state;
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void EmptyAudioView::SetDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    if (dcRt_) {
        dcRt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    }
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK EmptyAudioView::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EmptyAudioView* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<EmptyAudioView*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<EmptyAudioView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

D2D1_RECT_F EmptyAudioView::GetOpenButtonRect(float widthDip, float heightDip) const {
    float cx = widthDip * 0.5f;
    float cy = heightDip * 0.5f;
    float bw = 152.0f;
    float bh = 38.0f;
    float top = cy + 48.0f;
    return D2D1::RectF(cx - bw * 0.5f, top, cx + bw * 0.5f, top + bh);
}

LRESULT EmptyAudioView::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
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
            float wDip = static_cast<float>(rc.right - rc.left) / scale;
            float hDip = static_cast<float>(rc.bottom - rc.top) / scale;
            float mx = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            float my = static_cast<float>(GET_Y_LPARAM(lp)) / scale;

            bool over = !state_.loaded && PointInRect(mx, my, GetOpenButtonRect(wDip, hDip));
            if (over != hoverOpenBtn_) {
                hoverOpenBtn_ = over;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, over ? IDC_HAND : IDC_ARROW));

            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);

            // Forward mouse move to parent so fullscreen auto-hide wakes up
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd_, &pt);
            ScreenToClient(GetParent(hwnd_), &pt);
            PostMessageW(GetParent(hwnd_), WM_MOUSEMOVE, wp, MAKELPARAM(pt.x, pt.y));
            return 0;
        }

        case WM_MOUSELEAVE:
            if (hoverOpenBtn_ || pressOpenBtn_) {
                hoverOpenBtn_ = false;
                pressOpenBtn_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(GetParent(hwnd_));
            if (!state_.loaded && hoverOpenBtn_) {
                pressOpenBtn_ = true;
                SetCapture(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (pressOpenBtn_) {
                pressOpenBtn_ = false;
                ReleaseCapture();
                RECT rc{};
                GetClientRect(hwnd_, &rc);
                float scale = static_cast<float>(dpi_) / 96.0f;
                float wDip = static_cast<float>(rc.right - rc.left) / scale;
                float hDip = static_cast<float>(rc.bottom - rc.top) / scale;
                float mx = static_cast<float>(GET_X_LPARAM(lp)) / scale;
                float my = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
                InvalidateRect(hwnd_, nullptr, FALSE);
                if (!state_.loaded && PointInRect(mx, my, GetOpenButtonRect(wDip, hDip)) && onOpenFile_) {
                    onOpenFile_();
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK:
            if (!hoverOpenBtn_ && onDoubleClick_) {
                onDoubleClick_();
            }
            return 0;

        case WM_MOUSEWHEEL:
            return SendMessageW(GetParent(hwnd_), msg, wp, lp);

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void EmptyAudioView::EnsureDeviceResources() {
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

void EmptyAudioView::DiscardDeviceResources() {
    dcRt_.Reset();
}

void EmptyAudioView::RenderToHdc(HDC destHdc) {
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

void EmptyAudioView::RenderTo(ID2D1RenderTarget* rt) {
    if (!rt) return;

    rt->BeginDraw();
    rt->Clear(Rgb(0x0E1015));

    D2D1_SIZE_F sz = rt->GetSize();
    float cx = sz.width * 0.5f;
    float cy = sz.height * 0.5f;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentSoft;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textPrimary;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textSecondary;

    rt->CreateSolidColorBrush(Rgb(0x161922), &cardBg);
    rt->CreateSolidColorBrush(Rgb(0x282E3E), &borderBrush);
    rt->CreateSolidColorBrush(Rgb(0x3B82F6), &accentBrush);
    rt->CreateSolidColorBrush(Rgb(0x3B82F6, 0.16f), &accentSoft);
    rt->CreateSolidColorBrush(Rgb(0xF2F4F8), &textPrimary);
    rt->CreateSolidColorBrush(Rgb(0x98A2B3), &textSecondary);

    float cardW = std::min(460.0f, std::max(260.0f, sz.width - 48.0f));
    float cardH = 236.0f;
    D2D1_ROUNDED_RECT card{
        D2D1::RectF(cx - cardW * 0.5f, cy - cardH * 0.5f, cx + cardW * 0.5f, cy + cardH * 0.5f),
        14.0f, 14.0f
    };

    rt->FillRoundedRectangle(card, cardBg.Get());
    rt->DrawRoundedRectangle(card, borderBrush.Get(), 1.2f);

    if (!state_.loaded) {
        // Icon badge
        float iconY = cy - 58.0f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, iconY), 28.0f, 28.0f), accentSoft.Get());
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, iconY), 28.0f, 28.0f), accentBrush.Get(), 1.5f);

        // Play triangle inside icon
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> tri;
        if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&tri))) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(tri->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(cx - 7.0f, iconY - 11.0f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + 12.0f, iconY));
                sink->AddLine(D2D1::Point2F(cx - 7.0f, iconY + 11.0f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                rt->FillGeometry(tri.Get(), accentBrush.Get());
            }
        }

        if (titleFormat_) {
            std::wstring title = L"Play Everything";
            D2D1_RECT_F tr = D2D1::RectF(cx - cardW * 0.45f, cy - 22.0f, cx + cardW * 0.45f, cy + 10.0f);
            rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(), tr, textPrimary.Get());
        }
        if (subFormat_) {
            std::wstring sub = L"Drop any video or audio file here";
            D2D1_RECT_F sr = D2D1::RectF(cx - cardW * 0.45f, cy + 12.0f, cx + cardW * 0.45f, cy + 36.0f);
            rt->DrawTextW(sub.c_str(), static_cast<UINT32>(sub.size()), subFormat_.Get(), sr, textSecondary.Get());
        }

        // Open File button
        D2D1_RECT_F br = GetOpenButtonRect(sz.width, sz.height);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnFill;
        uint32_t btnHex = pressOpenBtn_ ? 0x2563EB : (hoverOpenBtn_ ? 0x60A5FA : 0x3B82F6);
        rt->CreateSolidColorBrush(Rgb(btnHex), &btnFill);
        rt->FillRoundedRectangle(D2D1::RoundedRect(br, 8.0f, 8.0f), btnFill.Get());

        if (btnFormat_) {
            std::wstring lbl = L"Open File...";
            rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), btnFormat_.Get(), br, textPrimary.Get());
        }
    } else {
        // Audio-only playback card
        float iconY = cy - 52.0f;
        bool playing = !state_.paused && !state_.eofReached;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, iconY), 30.0f, 30.0f), accentSoft.Get());
        rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, iconY), 30.0f, 30.0f),
            playing ? accentBrush.Get() : borderBrush.Get(), 1.8f);

        // Minimalist music note icon
        ID2D1SolidColorBrush* noteBrush = playing ? accentBrush.Get() : textSecondary.Get();
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 7.0f, iconY + 9.0f), 5.0f, 4.0f), noteBrush);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 8.0f, iconY + 6.0f), 5.0f, 4.0f), noteBrush);
        rt->DrawLine(D2D1::Point2F(cx - 2.5f, iconY + 8.0f), D2D1::Point2F(cx - 2.5f, iconY - 11.0f), noteBrush, 2.2f);
        rt->DrawLine(D2D1::Point2F(cx + 12.5f, iconY + 5.0f), D2D1::Point2F(cx + 12.5f, iconY - 14.0f), noteBrush, 2.2f);
        rt->DrawLine(D2D1::Point2F(cx - 3.0f, iconY - 11.0f), D2D1::Point2F(cx + 13.0f, iconY - 14.0f), noteBrush, 3.4f);

        if (titleFormat_) {
            std::wstring title = state_.mediaTitle.empty() ? L"Audio Track" : state_.mediaTitle;
            D2D1_RECT_F tr = D2D1::RectF(cx - cardW * 0.44f, cy - 8.0f, cx + cardW * 0.44f, cy + 28.0f);
            rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), titleFormat_.Get(), tr, textPrimary.Get());
        }

        // Metadata subtitle (codec, sample rate, channels)
        std::wstring meta;
        if (!state_.audioCodec.empty()) {
            meta = UpperAscii(state_.audioCodec);
        }
        if (state_.audioSampleRate > 0) {
            wchar_t khz[32]{};
            std::swprintf(khz, std::size(khz), L"%.1f kHz", state_.audioSampleRate / 1000.0);
            if (!meta.empty()) meta += L"  •  ";
            meta += khz;
        }
        if (state_.audioChannels > 0) {
            if (!meta.empty()) meta += L"  •  ";
            if (state_.audioChannels == 1) meta += L"Mono";
            else if (state_.audioChannels == 2) meta += L"Stereo";
            else meta += std::to_wstring(state_.audioChannels) + L" ch";
        }
        if (meta.empty()) {
            meta = playing ? L"Playing Audio" : L"Paused";
        }

        if (subFormat_) {
            D2D1_RECT_F sr = D2D1::RectF(cx - cardW * 0.44f, cy + 32.0f, cx + cardW * 0.44f, cy + 56.0f);
            rt->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), subFormat_.Get(), sr, textSecondary.Get());

            std::wstring status = state_.eofReached ? L"Finished" : (state_.paused ? L"Paused" : L"Playing");
            if (state_.loopEnabled) status += L"  •  Loop On";
            D2D1_RECT_F statusR = D2D1::RectF(cx - cardW * 0.44f, cy + 62.0f, cx + cardW * 0.44f, cy + 84.0f);
            rt->DrawTextW(status.c_str(), static_cast<UINT32>(status.size()), subFormat_.Get(), statusR,
                          playing ? accentBrush.Get() : textSecondary.Get());
        }
    }

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

}  // namespace playeverything
