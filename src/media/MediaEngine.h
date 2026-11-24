#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct mpv_handle;

namespace playeverything {

struct MediaInfoEntry {
    std::wstring section;
    std::wstring key;
    std::wstring value;
};

struct MediaState {
    bool loaded = false;
    bool paused = true;
    bool eofReached = false;
    bool loopEnabled = false;
    bool muted = false;
    bool hasVideo = false;
    double timePos = 0.0;
    double duration = 0.0;
    double volume = 100.0;
    double speed = 1.0;
    double fps = 0.0;
    int videoWidth = 0;
    int videoHeight = 0;
    int audioSampleRate = 0;
    int audioChannels = 0;
    std::wstring filePath;
    std::wstring mediaTitle;
    std::wstring videoCodec;
    std::wstring audioCodec;
    std::wstring lastError;
};

class MediaEngine {
public:
    MediaEngine() = default;
    ~MediaEngine();

    MediaEngine(const MediaEngine&) = delete;
    MediaEngine& operator=(const MediaEngine&) = delete;

    bool Initialize(HWND videoHostHwnd, HWND notifyHwnd, UINT wakeupMsg, bool headless = false);
    void Shutdown();

    bool IsInitialized() const { return mpv_ != nullptr; }

    // Drain all pending libmpv events on the calling thread. Returns true if state changed.
    bool ProcessEvents(double waitTimeoutSec = 0.0);

    bool OpenFile(const std::wstring& path);
    void CloseMedia();

    void Play();
    void Pause();
    void TogglePlayPause();
    void Restart();
    void SeekRelative(double deltaSeconds);
    void SeekAbsolute(double seconds, bool exact);
    void SetLoop(bool enabled);
    void ToggleLoop();
    void SetVolume(double volumePercent);
    void AdjustVolume(double deltaPercent);
    void SetMute(bool muted);
    void ToggleMute();
    void SetSpeed(double speed);

    std::vector<MediaInfoEntry> QueryMediaInfo() const;

    const MediaState& State() const { return state_; }
    void ClearLastError() { state_.lastError.clear(); }

private:
    static void OnMpvWakeup(void* ctx);

    void HandlePropertyChange(uint64_t userdata, const void* propPtr);

    mpv_handle* mpv_ = nullptr;
    HWND notifyHwnd_ = nullptr;
    UINT wakeupMsg_ = 0;
    MediaState state_;
};

}  // namespace playeverything
