#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playeverything {

struct ThumbnailFrame {
    int width = 0;
    int height = 0;
    double timestamp = -1.0;
    std::vector<uint8_t> bgra;  // top-down BGRA, 4 bytes per pixel
};

class ThumbnailProvider {
public:
    ThumbnailProvider() = default;
    ~ThumbnailProvider();

    ThumbnailProvider(const ThumbnailProvider&) = delete;
    ThumbnailProvider& operator=(const ThumbnailProvider&) = delete;

    void Start(HWND notifyHwnd, UINT readyMsg);
    void Stop();

    void SetMedia(const std::wstring& path, bool hasVideo);
    void ClearMedia();
    void RequestFrame(double seconds);

    bool CopyLatestFrame(ThumbnailFrame& outFrame) const;

private:
    void WorkerLoop();

    HWND notifyHwnd_ = nullptr;
    UINT readyMsg_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;

    uint64_t mediaGen_ = 0;
    std::wstring mediaPath_;
    bool mediaHasVideo_ = false;

    uint64_t reqGen_ = 0;
    double reqSeconds_ = -1.0;

    ThumbnailFrame latestFrame_;
    std::thread worker_;
};

}  // namespace playeverything
