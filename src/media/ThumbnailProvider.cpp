#include "media/ThumbnailProvider.h"

#include <mpv/client.h>
#include <mpv/render.h>

#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>

namespace playeverything {

namespace {

constexpr int kThumbWidth = 256;
constexpr int kThumbHeight = 144;
constexpr size_t kThumbStride = kThumbWidth * 4;  // 1024 bytes (64-byte aligned)

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr, nullptr);
    return out;
}

}  // namespace

ThumbnailProvider::~ThumbnailProvider() {
    Stop();
}

void ThumbnailProvider::Start(HWND notifyHwnd, UINT readyMsg) {
    Stop();
    notifyHwnd_ = notifyHwnd;
    readyMsg_ = readyMsg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
    }
    worker_ = std::thread(&ThumbnailProvider::WorkerLoop, this);
}

void ThumbnailProvider::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    notifyHwnd_ = nullptr;
    readyMsg_ = 0;
}

void ThumbnailProvider::SetMedia(const std::wstring& path, bool hasVideo) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mediaPath_ == path && mediaHasVideo_ == hasVideo) {
            return;
        }
        mediaPath_ = path;
        mediaHasVideo_ = hasVideo;
        ++mediaGen_;
        reqSeconds_ = -1.0;
        latestFrame_ = ThumbnailFrame{};
    }
    cv_.notify_one();
}

void ThumbnailProvider::ClearMedia() {
    SetMedia(L"", false);
}

void ThumbnailProvider::RequestFrame(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mediaHasVideo_ || mediaPath_.empty()) return;
        reqSeconds_ = seconds;
        ++reqGen_;
    }
    cv_.notify_one();
}

bool ThumbnailProvider::CopyLatestFrame(ThumbnailFrame& outFrame) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestFrame_.width <= 0 || latestFrame_.height <= 0 || latestFrame_.bgra.empty()) {
        return false;
    }
    outFrame = latestFrame_;
    return true;
}

void ThumbnailProvider::WorkerLoop() {
    std::setlocale(LC_NUMERIC, "C");

    mpv_handle* mpv = mpv_create();
    if (!mpv) return;

    mpv_set_option_string(mpv, "config", "no");
    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "ao", "null");
    mpv_set_option_string(mpv, "aid", "no");
    mpv_set_option_string(mpv, "sid", "no");
    mpv_set_option_string(mpv, "osc", "no");
    mpv_set_option_string(mpv, "osd-level", "0");
    mpv_set_option_string(mpv, "pause", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "hr-seek", "no");
    mpv_set_option_string(mpv, "hwdec", "no");
    mpv_set_option_string(mpv, "sws-scaler", "fast-bilinear");
    mpv_set_option_string(mpv, "vd-lavc-fast", "yes");
    mpv_set_option_string(mpv, "vd-lavc-skiploopfilter", "nonref");
    mpv_set_option_string(mpv, "audio-display", "no");
    mpv_set_option_string(mpv, "demuxer-max-bytes", "16MiB");
    mpv_set_option_string(mpv, "demuxer-readahead-secs", "1");

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        return;
    }

    mpv_render_param createParams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context* rctx = nullptr;
    if (mpv_render_context_create(&rctx, mpv, createParams) < 0 || !rctx) {
        mpv_terminate_destroy(mpv);
        return;
    }

    uint64_t loadedMediaGen = 0;
    bool fileLoaded = false;
    uint64_t processedReqGen = 0;
    double lastDecodedSec = -100.0;
    std::vector<uint8_t> pixelBuf(kThumbStride * kThumbHeight, 0);

    auto shouldInterrupt = [&](uint64_t curMediaGen) -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_ || mediaGen_ != curMediaGen;
    };

    while (true) {
        uint64_t curMediaGen = 0;
        std::wstring curPath;
        bool curHasVideo = false;
        uint64_t curReqGen = 0;
        double curReqSec = -1.0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&]() {
                return stop_ || mediaGen_ != loadedMediaGen ||
                       (fileLoaded && reqGen_ != processedReqGen && reqSeconds_ >= 0.0);
            });
            if (stop_) break;

            curMediaGen = mediaGen_;
            curPath = mediaPath_;
            curHasVideo = mediaHasVideo_;
            curReqGen = reqGen_;
            curReqSec = reqSeconds_;
        }

        // Load or clear media if changed
        if (curMediaGen != loadedMediaGen) {
            loadedMediaGen = curMediaGen;
            fileLoaded = false;
            lastDecodedSec = -100.0;
            processedReqGen = 0;

            if (!curHasVideo || curPath.empty()) {
                const char* stopCmd[] = {"stop", nullptr};
                mpv_command(mpv, stopCmd);
                continue;
            }

            std::string utf8 = WideToUtf8(curPath);
            if (utf8.empty()) continue;

            int pauseFlag = 1;
            mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pauseFlag);

            const char* loadCmd[] = {"loadfile", utf8.c_str(), "replace", nullptr};
            if (mpv_command(mpv, loadCmd) < 0) continue;

            // Wait up to 3s for file to load
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
            while (std::chrono::steady_clock::now() < deadline && !shouldInterrupt(curMediaGen)) {
                mpv_event* ev = mpv_wait_event(mpv, 0.03);
                if (!ev) break;
                if (ev->event_id == MPV_EVENT_FILE_LOADED || ev->event_id == MPV_EVENT_VIDEO_RECONFIG) {
                    fileLoaded = true;
                    break;
                }
                if (ev->event_id == MPV_EVENT_END_FILE) {
                    auto* ef = static_cast<mpv_event_end_file*>(ev->data);
                    if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                        fileLoaded = false;
                        break;
                    }
                }
            }
            continue;
        }

        if (!fileLoaded || curReqSec < 0.0) continue;
        processedReqGen = curReqGen;

        if (std::abs(curReqSec - lastDecodedSec) < 0.20) {
            continue;
        }

        char seekBuf[64]{};
        std::snprintf(seekBuf, sizeof(seekBuf), "%.2f", curReqSec);
        const char* seekCmd[] = {"seek", seekBuf, "absolute+keyframes", nullptr};
        if (mpv_command(mpv, seekCmd) < 0) continue;

        // Wait briefly for seek & frame decode
        bool frameReady = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
        while (std::chrono::steady_clock::now() < deadline && !shouldInterrupt(curMediaGen)) {
            mpv_event* ev = mpv_wait_event(mpv, 0.015);
            if (ev && ev->event_id == MPV_EVENT_PLAYBACK_RESTART) {
                frameReady = true;
                break;
            }
            if ((mpv_render_context_update(rctx) & MPV_RENDER_UPDATE_FRAME) != 0) {
                frameReady = true;
                break;
            }
        }

        if (shouldInterrupt(curMediaGen)) continue;
        if (!frameReady) {
            mpv_render_context_update(rctx);
        }

        int swSize[2] = {kThumbWidth, kThumbHeight};
        size_t stride = kThumbStride;
        int blockForTarget = 0;
        mpv_render_param renderParams[] = {
            {MPV_RENDER_PARAM_SW_SIZE, swSize},
            {MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("bgr0")},
            {MPV_RENDER_PARAM_SW_STRIDE, &stride},
            {MPV_RENDER_PARAM_SW_POINTER, pixelBuf.data()},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTarget},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        if (mpv_render_context_render(rctx, renderParams) >= 0) {
            // Set opaque alpha channel for Win32 DIB rendering
            for (size_t i = 3; i < pixelBuf.size(); i += 4) {
                pixelBuf[i] = 0xFF;
            }
            lastDecodedSec = curReqSec;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (mediaGen_ == curMediaGen && !stop_) {
                    latestFrame_.width = kThumbWidth;
                    latestFrame_.height = kThumbHeight;
                    latestFrame_.timestamp = curReqSec;
                    latestFrame_.bgra = pixelBuf;
                }
            }
            if (notifyHwnd_ && readyMsg_ != 0) {
                PostMessageW(notifyHwnd_, readyMsg_, 0, 0);
            }
        }
    }

    mpv_render_context_free(rctx);
    mpv_terminate_destroy(mpv);
}

}  // namespace playeverything
