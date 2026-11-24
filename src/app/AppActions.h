#pragma once

#include <optional>
#include <string>
#include <vector>

namespace playeverything {

enum class PlayerAction : int {
    PlayPause = 0,
    Play,
    Pause,
    Restart,
    SeekBack15,
    SeekForward15,
    ToggleLoop,
    VolumeUp,
    VolumeDown,
    ToggleMute,
    ToggleFullscreen,
    OpenFile,
    NextTrack,
    PrevTrack,
    ToggleShuffle,
    TogglePlaylist,
    ShowInfo,
    Count
};

inline const std::vector<PlayerAction>& AllActions() {
    static const std::vector<PlayerAction> kActions = {
        PlayerAction::PlayPause,
        PlayerAction::Play,
        PlayerAction::Pause,
        PlayerAction::Restart,
        PlayerAction::SeekBack15,
        PlayerAction::SeekForward15,
        PlayerAction::ToggleLoop,
        PlayerAction::ToggleShuffle,
        PlayerAction::VolumeUp,
        PlayerAction::VolumeDown,
        PlayerAction::ToggleMute,
        PlayerAction::ToggleFullscreen,
        PlayerAction::OpenFile,
        PlayerAction::NextTrack,
        PlayerAction::PrevTrack,
        PlayerAction::TogglePlaylist,
        PlayerAction::ShowInfo,
    };
    return kActions;
}

inline std::wstring ActionDisplayName(PlayerAction action) {
    switch (action) {
        case PlayerAction::PlayPause:        return L"Play / Pause";
        case PlayerAction::Play:             return L"Play";
        case PlayerAction::Pause:            return L"Pause";
        case PlayerAction::Restart:          return L"Restart";
        case PlayerAction::SeekBack15:       return L"Backward 15s";
        case PlayerAction::SeekForward15:    return L"Forward 15s";
        case PlayerAction::ToggleLoop:       return L"Toggle Loop";
        case PlayerAction::ToggleShuffle:    return L"Toggle Shuffle";
        case PlayerAction::VolumeUp:         return L"Volume Up";
        case PlayerAction::VolumeDown:       return L"Volume Down";
        case PlayerAction::ToggleMute:       return L"Toggle Mute";
        case PlayerAction::ToggleFullscreen: return L"Toggle Fullscreen";
        case PlayerAction::OpenFile:         return L"Open File";
        case PlayerAction::NextTrack:        return L"Next Playlist Item";
        case PlayerAction::PrevTrack:        return L"Previous Playlist Item";
        case PlayerAction::TogglePlaylist:   return L"Toggle Playlist Panel";
        case PlayerAction::ShowInfo:         return L"Media Info";
        default:                             return L"Unknown";
    }
}

inline std::string ActionId(PlayerAction action) {
    switch (action) {
        case PlayerAction::PlayPause:        return "play_pause";
        case PlayerAction::Play:             return "play";
        case PlayerAction::Pause:            return "pause";
        case PlayerAction::Restart:          return "restart";
        case PlayerAction::SeekBack15:       return "seek_back_15";
        case PlayerAction::SeekForward15:    return "seek_forward_15";
        case PlayerAction::ToggleLoop:       return "toggle_loop";
        case PlayerAction::ToggleShuffle:    return "toggle_shuffle";
        case PlayerAction::VolumeUp:         return "volume_up";
        case PlayerAction::VolumeDown:       return "volume_down";
        case PlayerAction::ToggleMute:       return "toggle_mute";
        case PlayerAction::ToggleFullscreen: return "toggle_fullscreen";
        case PlayerAction::OpenFile:         return "open_file";
        case PlayerAction::NextTrack:        return "next_track";
        case PlayerAction::PrevTrack:        return "prev_track";
        case PlayerAction::TogglePlaylist:   return "toggle_playlist";
        case PlayerAction::ShowInfo:         return "show_info";
        default:                             return "unknown";
    }
}

inline std::optional<PlayerAction> ActionFromId(const std::string& id) {
    for (PlayerAction action : AllActions()) {
        if (ActionId(action) == id) {
            return action;
        }
    }
    return std::nullopt;
}

}  // namespace playeverything
