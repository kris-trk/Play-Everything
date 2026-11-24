#include "ui/TimelinePreviewPopup.h"

#include "app/TimeFormat.h"

#include <algorithm>

namespace playeverything {

namespace {

constexpr wchar_t kPreviewClass[] = L"PlayEverythingTimelinePreview";

int ScalePx(int dip, UINT dpi) {
    return MulDiv(dip, static_cast<int>(dpi), 96);
}

}  // namespace

TimelinePreviewPopup::~TimelinePreviewPopup() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

bool TimelinePreviewPopup::Create(HWND owner, HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &TimelinePreviewPopup::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPreviewClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kPreviewClass, L"",
        WS_POPUP,
        0, 0, 224, 150,
        owner, nullptr, hInstance, this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(owner);
    if (dpi_ == 0) dpi_ = 96;
    RecreateFont();
    return true;
}

void TimelinePreviewPopup::SetDpi(UINT dpi) {
    dpi_ = dpi ? dpi : 96;
    RecreateFont();
}

void TimelinePreviewPopup::RecreateFont() {
    if (font_) DeleteObject(font_);
    font_ = CreateFontW(
        -MulDiv(9, static_cast<int>(dpi_), 72),
        0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

void TimelinePreviewPopup::ShowAt(int anchorScreenX, int barTopScreenY, double seconds, double duration,
                                  bool hasVideo, const RECT& ownerScreenBounds) {
    if (!hwnd_) return;

    hasVideo_ = hasVideo;
    hoverSeconds_ = seconds;
    duration_ = duration;

    int w = hasVideo ? ScalePx(228, dpi_) : ScalePx(76, dpi_);
    int h = hasVideo ? ScalePx(154, dpi_) : ScalePx(30, dpi_);
    int margin = ScalePx(8, dpi_);

    int x = anchorScreenX - w / 2;
    int minX = ownerScreenBounds.left + margin;
    int maxX = ownerScreenBounds.right - w - margin;
    if (maxX >= minX) {
        x = std::clamp(x, minX, maxX);
    }
    int y = barTopScreenY - h - ScalePx(8, dpi_);

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelinePreviewPopup::Hide() {
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void TimelinePreviewPopup::UpdateFrame(const ThumbnailFrame& frame) {
    frame_ = frame;
    if (hwnd_ && IsWindowVisible(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool TimelinePreviewPopup::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

LRESULT CALLBACK TimelinePreviewPopup::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TimelinePreviewPopup* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TimelinePreviewPopup*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<TimelinePreviewPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TimelinePreviewPopup::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd_, &ps);
            Paint(hdc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void TimelinePreviewPopup::Paint(HDC destHdc) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC memDc = CreateCompatibleDC(destHdc);
    HBITMAP bmp = CreateCompatibleBitmap(destHdc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDc, bmp);

    // Dark card background + subtle border
    HBRUSH bg = CreateSolidBrush(RGB(22, 25, 34));
    FillRect(memDc, &rc, bg);
    DeleteObject(bg);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(58, 66, 86));
    HGDIOBJ oldPen = SelectObject(memDc, borderPen);
    HGDIOBJ oldBrush = SelectObject(memDc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(memDc, 0, 0, w, h);
    SelectObject(memDc, oldBrush);
    SelectObject(memDc, oldPen);
    DeleteObject(borderPen);

    std::wstring timeTxt = FormatTimestamp(hoverSeconds_, duration_ >= 3600.0);
    SetBkMode(memDc, TRANSPARENT);
    SetTextColor(memDc, RGB(242, 244, 248));
    HGDIOBJ oldFont = SelectObject(memDc, font_);

    if (hasVideo_) {
        int pad = ScalePx(4, dpi_);
        int footerH = ScalePx(24, dpi_);
        int imgW = w - pad * 2;
        int imgH = h - pad * 2 - footerH;

        RECT imgRc{pad, pad, pad + imgW, pad + imgH};
        HBRUSH blackBrush = CreateSolidBrush(RGB(10, 12, 16));
        FillRect(memDc, &imgRc, blackBrush);
        DeleteObject(blackBrush);

        if (frame_.width > 0 && frame_.height > 0 && !frame_.bgra.empty()) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = frame_.width;
            bmi.bmiHeader.biHeight = -frame_.height;  // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            SetStretchBltMode(memDc, HALFTONE);
            SetBrushOrgEx(memDc, 0, 0, nullptr);
            StretchDIBits(
                memDc,
                imgRc.left, imgRc.top, imgW, imgH,
                0, 0, frame_.width, frame_.height,
                frame_.bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        } else {
            SetTextColor(memDc, RGB(148, 158, 176));
            DrawTextW(memDc, L"Preview...", -1, &imgRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(memDc, RGB(242, 244, 248));
        }

        RECT txtRc{pad, imgRc.bottom + 2, w - pad, h - pad};
        DrawTextW(memDc, timeTxt.c_str(), -1, &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        DrawTextW(memDc, timeTxt.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(memDc, oldFont);
    BitBlt(destHdc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDc);
}

}  // namespace playeverything
