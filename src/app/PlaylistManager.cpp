#include "app/PlaylistManager.h"

#include <algorithm>
#include <numeric>

namespace playeverything {

PlaylistManager::PlaylistManager() : rng_(std::random_device{}()) {}

std::wstring PlaylistManager::FileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

void PlaylistManager::RebuildShuffleOrder(int avoidFirstIndex) {
    shuffleOrder_.clear();
    shufflePos_ = -1;
    if (items_.empty()) return;

    shuffleOrder_.resize(items_.size());
    std::iota(shuffleOrder_.begin(), shuffleOrder_.end(), 0);
    std::shuffle(shuffleOrder_.begin(), shuffleOrder_.end(), rng_);

    if (currentIndex_ >= 0 && currentIndex_ < static_cast<int>(items_.size()) && avoidFirstIndex < 0) {
        auto it = std::find(shuffleOrder_.begin(), shuffleOrder_.end(), currentIndex_);
        if (it != shuffleOrder_.end()) {
            std::iter_swap(shuffleOrder_.begin(), it);
        }
        shufflePos_ = 0;
    } else if (avoidFirstIndex >= 0 && shuffleOrder_.size() > 1 && shuffleOrder_.front() == avoidFirstIndex) {
        std::swap(shuffleOrder_[0], shuffleOrder_[1]);
    }
}

int PlaylistManager::AddFiles(const std::vector<std::wstring>& paths) {
    int firstAdded = -1;
    for (const auto& p : paths) {
        if (p.empty()) continue;
        if (firstAdded < 0) firstAdded = static_cast<int>(items_.size());
        items_.push_back(PlaylistItem{p, FileNameFromPath(p)});
    }
    if (shuffleEnabled_ && firstAdded >= 0) {
        RebuildShuffleOrder();
    }
    return firstAdded;
}

int PlaylistManager::AddOrSelectFile(const std::wstring& path) {
    if (path.empty()) return -1;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (_wcsicmp(items_[i].path.c_str(), path.c_str()) == 0) {
            SetCurrentIndex(static_cast<int>(i));
            return currentIndex_;
        }
    }
    items_.push_back(PlaylistItem{path, FileNameFromPath(path)});
    SetCurrentIndex(static_cast<int>(items_.size()) - 1);
    return currentIndex_;
}

bool PlaylistManager::RemoveAt(size_t index) {
    if (index >= items_.size()) return false;
    items_.erase(items_.begin() + static_cast<ptrdiff_t>(index));
    if (items_.empty()) {
        currentIndex_ = -1;
    } else if (currentIndex_ == static_cast<int>(index)) {
        currentIndex_ = std::min(currentIndex_, static_cast<int>(items_.size()) - 1);
    } else if (currentIndex_ > static_cast<int>(index)) {
        --currentIndex_;
    }
    if (shuffleEnabled_) {
        RebuildShuffleOrder();
    }
    return true;
}

bool PlaylistManager::MoveUp(size_t index) {
    if (index == 0 || index >= items_.size()) return false;
    std::swap(items_[index - 1], items_[index]);
    if (currentIndex_ == static_cast<int>(index)) {
        currentIndex_ = static_cast<int>(index - 1);
    } else if (currentIndex_ == static_cast<int>(index - 1)) {
        currentIndex_ = static_cast<int>(index);
    }
    if (shuffleEnabled_) {
        RebuildShuffleOrder();
    }
    return true;
}

bool PlaylistManager::MoveDown(size_t index) {
    if (index + 1 >= items_.size()) return false;
    std::swap(items_[index], items_[index + 1]);
    if (currentIndex_ == static_cast<int>(index)) {
        currentIndex_ = static_cast<int>(index + 1);
    } else if (currentIndex_ == static_cast<int>(index + 1)) {
        currentIndex_ = static_cast<int>(index);
    }
    if (shuffleEnabled_) {
        RebuildShuffleOrder();
    }
    return true;
}

void PlaylistManager::Clear() {
    items_.clear();
    currentIndex_ = -1;
    shuffleOrder_.clear();
    shufflePos_ = -1;
}

bool PlaylistManager::SetCurrentIndex(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return false;
    currentIndex_ = index;
    if (shuffleEnabled_) {
        RebuildShuffleOrder();
    }
    return true;
}

void PlaylistManager::SetShuffle(bool enabled) {
    if (shuffleEnabled_ == enabled) return;
    shuffleEnabled_ = enabled;
    if (shuffleEnabled_) {
        RebuildShuffleOrder();
    } else {
        shuffleOrder_.clear();
        shufflePos_ = -1;
    }
}

void PlaylistManager::ToggleShuffle() {
    SetShuffle(!shuffleEnabled_);
}

bool PlaylistManager::HasNext() const {
    if (items_.empty()) return false;
    if (shuffleEnabled_) {
        return items_.size() > 1;
    }
    return currentIndex_ + 1 < static_cast<int>(items_.size());
}

bool PlaylistManager::HasPrevious() const {
    if (items_.empty()) return false;
    if (shuffleEnabled_) {
        return shufflePos_ > 0;
    }
    return currentIndex_ > 0;
}

std::optional<std::wstring> PlaylistManager::Next() {
    if (!HasNext()) return std::nullopt;
    if (shuffleEnabled_) {
        if (shuffleOrder_.size() != items_.size()) {
            RebuildShuffleOrder();
        }
        if (shufflePos_ + 1 >= static_cast<int>(shuffleOrder_.size())) {
            int lastIdx = currentIndex_;
            RebuildShuffleOrder(lastIdx);
            shufflePos_ = 0;
        } else {
            ++shufflePos_;
        }
        currentIndex_ = shuffleOrder_[static_cast<size_t>(shufflePos_)];
        return items_[static_cast<size_t>(currentIndex_)].path;
    }
    ++currentIndex_;
    return items_[static_cast<size_t>(currentIndex_)].path;
}

std::optional<std::wstring> PlaylistManager::Previous() {
    if (!HasPrevious()) return std::nullopt;
    if (shuffleEnabled_) {
        --shufflePos_;
        currentIndex_ = shuffleOrder_[static_cast<size_t>(shufflePos_)];
        return items_[static_cast<size_t>(currentIndex_)].path;
    }
    --currentIndex_;
    return items_[static_cast<size_t>(currentIndex_)].path;
}

}  // namespace playeverything
