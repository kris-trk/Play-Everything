#include "media/MediaEngine.h"

#include "app/TimeFormat.h"

#include <mpv/client.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace playeverything {

namespace {

enum PropId : uint64_t {
    PROP_TIME_POS = 1,
    PROP_DURATION,
    PROP_PAUSE,
    PROP_EOF_REACHED,
    PROP_LOOP_FILE,
    PROP_VOLUME,
    PROP_MUTE,
    PROP_SPEED,
    PROP_MEDIA_TITLE,
    PROP_VIDEO_W,
    PROP_VIDEO_H,
    PROP_VIDEO_CODEC,
    PROP_AUDIO_CODEC,
    PROP_AUDIO_SAMPLERATE,
    PROP_AUDIO_CHANNELS,
    PROP_FPS,
};

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), size - 1);
    return out;
}

std::wstring ExtractFileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

}  // namespace

MediaEngine::~MediaEngine() {
    Shutdown();
}

void MediaEngine::OnMpvWakeup(void* ctx) {
    auto* self = static_cast<MediaEngine*>(ctx);
    if (self && self->notifyHwnd_ && self->wakeupMsg_ != 0) {
        PostMessageW(self->notifyHwnd_, self->wakeupMsg_, 0, 0);
    }
}

bool MediaEngine::Initialize(HWND videoHostHwnd, HWND notifyHwnd, UINT wakeupMsg, bool headless) {
    Shutdown();

    std::setlocale(LC_NUMERIC, "C");

    mpv_ = mpv_create();
    if (!mpv_) {
        state_.lastError = L"Failed to create libmpv instance.";
        return false;
    }

    notifyHwnd_ = notifyHwnd;
    wakeupMsg_ = wakeupMsg;

    mpv_set_option_string(mpv_, "config", "no");
    mpv_set_option_string(mpv_, "idle", "yes");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "osc", "no");
    mpv_set_option_string(mpv_, "osd-level", "0");
    mpv_set_option_string(mpv_, "input-default-bindings", "no");
    mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
    mpv_set_option_string(mpv_, "input-cursor", "no");
    mpv_set_option_string(mpv_, "cursor-autohide", "no");
    mpv_set_option_string(mpv_, "loop-file", "no");

    if (headless) {
        mpv_set_option_string(mpv_, "vo", "null");
        mpv_set_option_string(mpv_, "ao", "null");
    } else {
        int64_t wid = reinterpret_cast<int64_t>(videoHostHwnd);
        mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
        mpv_set_option_string(mpv_, "profile", "high-quality");
        mpv_set_option_string(mpv_, "vo", "gpu");
        mpv_set_option_string(mpv_, "hwdec", "auto-safe");
        mpv_set_option_string(mpv_, "ao", "wasapi");
        mpv_set_option_string(mpv_, "video-sync", "audio");
        mpv_set_option_string(mpv_, "audio-display", "embedded-first");
        mpv_set_option_string(mpv_, "framedrop", "vo");
    }

    int err = mpv_initialize(mpv_);
    if (err < 0) {
        state_.lastError = L"Failed to initialize libmpv: " + Utf8ToWide(mpv_error_string(err));
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_observe_property(mpv_, PROP_TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, PROP_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, PROP_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, PROP_EOF_REACHED, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, PROP_LOOP_FILE, "loop-file", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, PROP_VOLUME, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, PROP_MUTE, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, PROP_SPEED, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, PROP_MEDIA_TITLE, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, PROP_VIDEO_W, "video-params/w", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, PROP_VIDEO_H, "video-params/h", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, PROP_VIDEO_CODEC, "video-codec", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, PROP_AUDIO_CODEC, "audio-codec-name", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, PROP_AUDIO_SAMPLERATE, "audio-params/samplerate", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, PROP_AUDIO_CHANNELS, "audio-params/channel-count", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, PROP_FPS, "container-fps", MPV_FORMAT_DOUBLE);

    if (notifyHwnd_ && wakeupMsg_ != 0) {
        mpv_set_wakeup_callback(mpv_, &MediaEngine::OnMpvWakeup, this);
    }

    state_ = MediaState{};
    state_.volume = 100.0;
    return true;
}

void MediaEngine::Shutdown() {
    if (mpv_) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    notifyHwnd_ = nullptr;
    wakeupMsg_ = 0;
}

bool MediaEngine::ProcessEvents(double waitTimeoutSec) {
    if (!mpv_) return false;

    bool changed = false;
    double timeout = waitTimeoutSec;

    while (true) {
        mpv_event* ev = mpv_wait_event(mpv_, timeout);
        timeout = 0.0;
        if (!ev || ev->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (ev->event_id) {
            case MPV_EVENT_START_FILE:
                state_.lastError.clear();
                state_.eofReached = false;
                state_.hasVideo = false;
                state_.videoWidth = 0;
                state_.videoHeight = 0;
                state_.videoCodec.clear();
                state_.audioCodec.clear();
                state_.audioSampleRate = 0;
                state_.audioChannels = 0;
                state_.fps = 0.0;
                changed = true;
                break;

            case MPV_EVENT_FILE_LOADED:
                state_.loaded = true;
                state_.eofReached = false;
                changed = true;
                break;

            case MPV_EVENT_END_FILE: {
                auto* ef = static_cast<mpv_event_end_file*>(ev->data);
                if (ef) {
                    if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                        state_.loaded = false;
                        state_.paused = true;
                        state_.hasVideo = false;
                        state_.lastError = L"Could not play file: " + Utf8ToWide(mpv_error_string(ef->error));
                        changed = true;
                    } else if (ef->reason == MPV_END_FILE_REASON_STOP) {
                        state_.loaded = false;
                        state_.paused = true;
                        state_.eofReached = false;
                        state_.hasVideo = false;
                        state_.timePos = 0.0;
                        state_.duration = 0.0;
                        state_.filePath.clear();
                        state_.mediaTitle.clear();
                        changed = true;
                    }
                }
                break;
            }

            case MPV_EVENT_PROPERTY_CHANGE:
                HandlePropertyChange(ev->reply_userdata, ev->data);
                changed = true;
                break;

            default:
                break;
        }
    }

    return changed;
}

void MediaEngine::HandlePropertyChange(uint64_t userdata, const void* propPtr) {
    const auto* prop = static_cast<const mpv_event_property*>(propPtr);
    if (!prop) return;

    switch (userdata) {
        case PROP_TIME_POS:
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                state_.timePos = *static_cast<const double*>(prop->data);
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.timePos = 0.0;
            }
            break;

        case PROP_DURATION:
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                state_.duration = *static_cast<const double*>(prop->data);
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.duration = 0.0;
            }
            break;

        case PROP_PAUSE:
            if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                state_.paused = (*static_cast<const int*>(prop->data)) != 0;
            }
            break;

        case PROP_EOF_REACHED:
            if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                state_.eofReached = (*static_cast<const int*>(prop->data)) != 0;
            }
            break;

        case PROP_LOOP_FILE:
            if (prop->format == MPV_FORMAT_STRING && prop->data) {
                const char* val = *static_cast<const char* const*>(prop->data);
                state_.loopEnabled = (val && (std::string(val) == "inf" || std::string(val) == "yes"));
            }
            break;

        case PROP_VOLUME:
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                state_.volume = std::clamp(*static_cast<const double*>(prop->data), 0.0, 100.0);
            }
            break;

        case PROP_MUTE:
            if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                state_.muted = (*static_cast<const int*>(prop->data)) != 0;
            }
            break;

        case PROP_SPEED:
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                state_.speed = std::clamp(*static_cast<const double*>(prop->data), 0.1, 16.0);
            }
            break;

        case PROP_MEDIA_TITLE:
            if (prop->format == MPV_FORMAT_STRING && prop->data) {
                const char* val = *static_cast<const char* const*>(prop->data);
                std::wstring title = Utf8ToWide(val);
                if (!title.empty()) {
                    state_.mediaTitle = title;
                }
            }
            break;

        case PROP_VIDEO_W:
            if (prop->format == MPV_FORMAT_INT64 && prop->data) {
                state_.videoWidth = static_cast<int>(*static_cast<const int64_t*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.videoWidth = 0;
            }
            state_.hasVideo = (state_.videoWidth > 0 && state_.videoHeight > 0);
            break;

        case PROP_VIDEO_H:
            if (prop->format == MPV_FORMAT_INT64 && prop->data) {
                state_.videoHeight = static_cast<int>(*static_cast<const int64_t*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.videoHeight = 0;
            }
            state_.hasVideo = (state_.videoWidth > 0 && state_.videoHeight > 0);
            break;

        case PROP_VIDEO_CODEC:
            if (prop->format == MPV_FORMAT_STRING && prop->data) {
                state_.videoCodec = Utf8ToWide(*static_cast<const char* const*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.videoCodec.clear();
            }
            break;

        case PROP_AUDIO_CODEC:
            if (prop->format == MPV_FORMAT_STRING && prop->data) {
                state_.audioCodec = Utf8ToWide(*static_cast<const char* const*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.audioCodec.clear();
            }
            break;

        case PROP_AUDIO_SAMPLERATE:
            if (prop->format == MPV_FORMAT_INT64 && prop->data) {
                state_.audioSampleRate = static_cast<int>(*static_cast<const int64_t*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.audioSampleRate = 0;
            }
            break;

        case PROP_AUDIO_CHANNELS:
            if (prop->format == MPV_FORMAT_INT64 && prop->data) {
                state_.audioChannels = static_cast<int>(*static_cast<const int64_t*>(prop->data));
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.audioChannels = 0;
            }
            break;

        case PROP_FPS:
            if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                state_.fps = *static_cast<const double*>(prop->data);
            } else if (prop->format == MPV_FORMAT_NONE) {
                state_.fps = 0.0;
            }
            break;

        default:
            break;
    }
}

bool MediaEngine::OpenFile(const std::wstring& path) {
    if (!mpv_ || path.empty()) return false;

    std::string utf8Path = WideToUtf8(path);
    if (utf8Path.empty()) return false;

    state_.filePath = path;
    state_.mediaTitle = ExtractFileName(path);
    state_.lastError.clear();

    // Ensure unpaused when opening a new file
    int pauseFlag = 0;
    mpv_set_property_async(mpv_, 0, "pause", MPV_FORMAT_FLAG, &pauseFlag);

    const char* cmd[] = {"loadfile", utf8Path.c_str(), "replace", nullptr};
    int err = mpv_command_async(mpv_, 0, cmd);
    return err >= 0;
}

void MediaEngine::CloseMedia() {
    if (!mpv_) return;
    const char* cmd[] = {"stop", nullptr};
    mpv_command_async(mpv_, 0, cmd);
    state_.loaded = false;
    state_.paused = true;
    state_.eofReached = false;
    state_.hasVideo = false;
    state_.timePos = 0.0;
    state_.duration = 0.0;
    state_.filePath.clear();
    state_.mediaTitle.clear();
}

void MediaEngine::Play() {
    if (!mpv_ || !state_.loaded) return;
    if (state_.eofReached) {
        const char* seekCmd[] = {"seek", "0", "absolute+exact", nullptr};
        mpv_command_async(mpv_, 0, seekCmd);
        state_.eofReached = false;
    }
    int pauseFlag = 0;
    mpv_set_property_async(mpv_, 0, "pause", MPV_FORMAT_FLAG, &pauseFlag);
}

void MediaEngine::Pause() {
    if (!mpv_ || !state_.loaded) return;
    int pauseFlag = 1;
    mpv_set_property_async(mpv_, 0, "pause", MPV_FORMAT_FLAG, &pauseFlag);
}

void MediaEngine::TogglePlayPause() {
    if (!mpv_ || !state_.loaded) return;
    if (state_.paused || state_.eofReached) {
        Play();
    } else {
        Pause();
    }
}

void MediaEngine::Restart() {
    if (!mpv_ || !state_.loaded) return;
    const char* seekCmd[] = {"seek", "0", "absolute+exact", nullptr};
    mpv_command_async(mpv_, 0, seekCmd);
    int pauseFlag = 0;
    mpv_set_property_async(mpv_, 0, "pause", MPV_FORMAT_FLAG, &pauseFlag);
    state_.eofReached = false;
}

void MediaEngine::SeekRelative(double deltaSeconds) {
    if (!mpv_ || !state_.loaded) return;
    double target = ClampSeekTarget(state_.timePos + deltaSeconds, state_.duration);
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.3f", target);
    const char* seekCmd[] = {"seek", buf, "absolute+exact", nullptr};
    mpv_command_async(mpv_, 0, seekCmd);
    state_.eofReached = false;
}

void MediaEngine::SeekAbsolute(double seconds, bool exact) {
    if (!mpv_ || !state_.loaded) return;
    double target = ClampSeekTarget(seconds, state_.duration);
    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.3f", target);
    const char* mode = exact ? "absolute+exact" : "absolute+keyframes";
    const char* seekCmd[] = {"seek", buf, mode, nullptr};
    mpv_command_async(mpv_, 0, seekCmd);
    state_.eofReached = false;
}

void MediaEngine::SetLoop(bool enabled) {
    if (!mpv_) return;
    const char* val = enabled ? "inf" : "no";
    mpv_set_property_string(mpv_, "loop-file", val);
    state_.loopEnabled = enabled;
}

void MediaEngine::ToggleLoop() {
    SetLoop(!state_.loopEnabled);
}

void MediaEngine::SetVolume(double volumePercent) {
    if (!mpv_) return;
    double clamped = std::clamp(volumePercent, 0.0, 100.0);
    mpv_set_property_async(mpv_, 0, "volume", MPV_FORMAT_DOUBLE, &clamped);
    state_.volume = clamped;
    if (clamped > 0.0 && state_.muted) {
        SetMute(false);
    }
}

void MediaEngine::AdjustVolume(double deltaPercent) {
    SetVolume(state_.volume + deltaPercent);
}

void MediaEngine::SetMute(bool muted) {
    if (!mpv_) return;
    int flag = muted ? 1 : 0;
    mpv_set_property_async(mpv_, 0, "mute", MPV_FORMAT_FLAG, &flag);
    state_.muted = muted;
}

void MediaEngine::ToggleMute() {
    SetMute(!state_.muted);
}

void MediaEngine::SetSpeed(double speed) {
    if (!mpv_) return;
    double clamped = std::clamp(speed, 0.1, 16.0);
    mpv_set_property_async(mpv_, 0, "speed", MPV_FORMAT_DOUBLE, &clamped);
    state_.speed = clamped;
}

std::vector<MediaInfoEntry> MediaEngine::QueryMediaInfo() const {
    std::vector<MediaInfoEntry> rows;
    if (!mpv_ || !state_.loaded) {
        rows.push_back({L"General", L"Status", L"No media loaded"});
        return rows;
    }

    auto getStr = [this](const char* prop) -> std::wstring {
        char* s = mpv_get_property_string(mpv_, prop);
        if (!s) return {};
        std::wstring out = Utf8ToWide(s);
        mpv_free(s);
        return out;
    };

    auto getDouble = [this](const char* prop, double& outVal) -> bool {
        return mpv_get_property(mpv_, prop, MPV_FORMAT_DOUBLE, &outVal) >= 0 && std::isfinite(outVal);
    };

    auto getInt = [this](const char* prop, int64_t& outVal) -> bool {
        return mpv_get_property(mpv_, prop, MPV_FORMAT_INT64, &outVal) >= 0;
    };

    auto formatBitrate = [](double bps) -> std::wstring {
        wchar_t buf[64]{};
        if (bps >= 1'000'000.0) {
            std::swprintf(buf, std::size(buf), L"%.2f Mbps", bps / 1'000'000.0);
        } else if (bps >= 1'000.0) {
            std::swprintf(buf, std::size(buf), L"%.0f kbps", bps / 1'000.0);
        } else {
            std::swprintf(buf, std::size(buf), L"%.0f bps", bps);
        }
        return buf;
    };

    auto formatBytes = [](uint64_t bytes) -> std::wstring {
        wchar_t buf[64]{};
        if (bytes >= (1ULL << 30)) {
            std::swprintf(buf, std::size(buf), L"%.2f GB", static_cast<double>(bytes) / (1ULL << 30));
        } else if (bytes >= (1ULL << 20)) {
            std::swprintf(buf, std::size(buf), L"%.2f MB", static_cast<double>(bytes) / (1ULL << 20));
        } else if (bytes >= (1ULL << 10)) {
            std::swprintf(buf, std::size(buf), L"%.1f KB", static_cast<double>(bytes) / (1ULL << 10));
        } else {
            std::swprintf(buf, std::size(buf), L"%llu B", static_cast<unsigned long long>(bytes));
        }
        return buf;
    };

    // General
    rows.push_back({L"General", L"File Name", ExtractFileName(state_.filePath)});
    rows.push_back({L"General", L"File Path", state_.filePath});

    uint64_t fileBytes = 0;
    {
        std::error_code ec;
        auto sz = std::filesystem::file_size(state_.filePath, ec);
        if (!ec) {
            fileBytes = sz;
            rows.push_back({L"General", L"File Size", formatBytes(fileBytes)});
        }
    }

    std::wstring fmt = getStr("file-format");
    if (!fmt.empty()) rows.push_back({L"General", L"Container Format", fmt});

    if (state_.duration > 0.0) {
        rows.push_back({L"General", L"Duration", FormatTimestamp(state_.duration, state_.duration >= 3600.0)});
        if (fileBytes > 0) {
            double overallBps = (static_cast<double>(fileBytes) * 8.0) / state_.duration;
            rows.push_back({L"General", L"Overall Bitrate", formatBitrate(overallBps)});
        }
    }

    {
        wchar_t spdBuf[32]{};
        std::swprintf(spdBuf, std::size(spdBuf), L"%.2fx", state_.speed);
        rows.push_back({L"General", L"Playback Speed", spdBuf});
    }

    // Video
    int64_t vw = 0, vh = 0;
    if (getInt("video-params/w", vw) && getInt("video-params/h", vh) && vw > 0 && vh > 0) {
        std::wstring res = std::to_wstring(vw) + L" \u00D7 " + std::to_wstring(vh);
        double aspect = 0.0;
        if (getDouble("video-params/aspect", aspect) && aspect > 0.0) {
            wchar_t abuf[32]{};
            std::swprintf(abuf, std::size(abuf), L" (%.2f:1)", aspect);
            res += abuf;
        }
        rows.push_back({L"Video", L"Resolution", res});

        double cfps = 0.0, efps = 0.0;
        bool hasCfps = getDouble("container-fps", cfps) && cfps > 0.0;
        bool hasEfps = getDouble("estimated-vf-fps", efps) && efps > 0.0;
        if (hasCfps || hasEfps) {
            wchar_t fbuf[64]{};
            double primary = hasCfps ? cfps : efps;
            std::swprintf(fbuf, std::size(fbuf), L"%.3f fps", primary);
            rows.push_back({L"Video", L"Framerate", fbuf});
        }

        std::wstring vcodec = getStr("video-codec");
        if (vcodec.empty()) vcodec = getStr("video-format");
        if (!vcodec.empty()) rows.push_back({L"Video", L"Video Codec", vcodec});

        double vbr = 0.0;
        if (getDouble("video-bitrate", vbr) && vbr > 0.0) {
            rows.push_back({L"Video", L"Video Bitrate", formatBitrate(vbr)});
        }

        std::wstring pixfmt = getStr("video-params/pixelformat");
        if (!pixfmt.empty()) rows.push_back({L"Video", L"Pixel Format", pixfmt});

        std::wstring primaries = getStr("video-params/primaries");
        std::wstring gamma = getStr("video-params/gamma");
        if (!primaries.empty() || !gamma.empty()) {
            std::wstring cs = primaries;
            if (!gamma.empty()) {
                if (!cs.empty()) cs += L" / ";
                cs += gamma;
            }
            rows.push_back({L"Video", L"Color Space", cs});
        }

        std::wstring hwdec = getStr("hwdec-current");
        if (!hwdec.empty() && hwdec != L"no") {
            rows.push_back({L"Video", L"Hardware Decoder", hwdec});
        } else {
            rows.push_back({L"Video", L"Hardware Decoder", L"Software (CPU)"});
        }
    }

    // Audio
    std::wstring acodec = getStr("audio-codec");
    if (acodec.empty()) acodec = getStr("audio-codec-name");
    if (!acodec.empty()) {
        rows.push_back({L"Audio", L"Audio Codec", acodec});

        double abr = 0.0;
        if (getDouble("audio-bitrate", abr) && abr > 0.0) {
            rows.push_back({L"Audio", L"Audio Bitrate", formatBitrate(abr)});
        }

        int64_t sr = 0;
        if (getInt("audio-params/samplerate", sr) && sr > 0) {
            wchar_t sbuf[32]{};
            std::swprintf(sbuf, std::size(sbuf), L"%.1f kHz (%lld Hz)", static_cast<double>(sr) / 1000.0, static_cast<long long>(sr));
            rows.push_back({L"Audio", L"Sample Rate", sbuf});
        }

        std::wstring ch = getStr("audio-params/hr-channels");
        int64_t chCount = 0;
        if (getInt("audio-params/channel-count", chCount) && chCount > 0) {
            if (ch.empty()) {
                ch = std::to_wstring(chCount) + L" ch";
            } else {
                ch += L" (" + std::to_wstring(chCount) + L" ch)";
            }
            rows.push_back({L"Audio", L"Channels", ch});
        }

        std::wstring afmt = getStr("audio-params/format");
        if (!afmt.empty()) rows.push_back({L"Audio", L"Sample Format", afmt});
    }

    // Metadata tags from container/streams
    mpv_node metaNode{};
    if (mpv_get_property(mpv_, "metadata", MPV_FORMAT_NODE, &metaNode) >= 0) {
        if (metaNode.format == MPV_FORMAT_NODE_MAP && metaNode.u.list) {
            const mpv_node_list* list = metaNode.u.list;
            for (int i = 0; i < list->num; ++i) {
                if (!list->keys || !list->keys[i]) continue;
                std::wstring k = Utf8ToWide(list->keys[i]);
                std::wstring v;
                const mpv_node& val = list->values[i];
                if (val.format == MPV_FORMAT_STRING && val.u.string) {
                    v = Utf8ToWide(val.u.string);
                } else if (val.format == MPV_FORMAT_INT64) {
                    v = std::to_wstring(val.u.int64);
                } else if (val.format == MPV_FORMAT_DOUBLE) {
                    v = std::to_wstring(val.u.double_);
                }
                if (!k.empty() && !v.empty()) {
                    rows.push_back({L"Metadata", k, v});
                }
            }
        }
        mpv_free_node_contents(&metaNode);
    }

    return rows;
}

}  // namespace playeverything
