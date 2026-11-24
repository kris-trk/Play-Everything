#pragma once

#include <optional>
#include <random>
#include <string>
#include <vector>

namespace playeverything {

struct PlaylistItem {
    std::wstring path;
    std::wstring displayName;
};

class PlaylistManager {
public:
    PlaylistManager();

    int AddFiles(const std::vector<std::wstring>& paths);
    int AddOrSelectFile(const std::wstring& path);
    bool RemoveAt(size_t index);
    bool MoveUp(size_t index);
    bool MoveDown(size_t index);
    void Clear();

    bool SetCurrentIndex(int index);
    std::optional<std::wstring> Next();
    std::optional<std::wstring> Previous();

    bool HasNext() const;
    bool HasPrevious() const;

    void SetShuffle(bool enabled);
    void ToggleShuffle();
    bool IsShuffleEnabled() const { return shuffleEnabled_; }

    int CurrentIndex() const { return currentIndex_; }
    const std::vector<PlaylistItem>& Items() const { return items_; }
    bool Empty() const { return items_.empty(); }
    size_t Size() const { return items_.size(); }

private:
    static std::wstring FileNameFromPath(const std::wstring& path);
    void RebuildShuffleOrder(int avoidFirstIndex = -1);

    std::vector<PlaylistItem> items_;
    int currentIndex_ = -1;

    bool shuffleEnabled_ = false;
    std::vector<int> shuffleOrder_;
    int shufflePos_ = -1;
    std::mt19937 rng_;
};

}  // namespace playeverything
