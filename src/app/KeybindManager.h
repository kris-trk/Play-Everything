#pragma once

#include "app/AppActions.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace playeverything {

struct KeyCombo {
    UINT vk = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;

    bool operator==(const KeyCombo& other) const = default;

    static bool IsModifierKey(UINT vk);
    bool IsValid() const;
};

std::wstring FormatKeyCombo(const KeyCombo& combo);
std::string FormatKeyComboUtf8(const KeyCombo& combo);
std::optional<KeyCombo> ParseKeyComboUtf8(const std::string& text);

class KeybindManager {
public:
    KeybindManager();

    void ResetDefaults();

    const std::vector<KeyCombo>& GetBindings(PlayerAction action) const;
    std::wstring GetBindingsSummary(PlayerAction action) const;
    std::wstring GetPrimaryShortcut(PlayerAction action) const;

    std::optional<PlayerAction> FindAction(const KeyCombo& combo) const;

    bool AddBinding(PlayerAction action, const KeyCombo& combo);
    bool ReplaceBinding(PlayerAction action, size_t index, const KeyCombo& combo);
    bool RemoveBinding(PlayerAction action, size_t index);
    void RemoveComboEverywhere(const KeyCombo& combo);

    std::string SerializeJson() const;
    bool DeserializeJson(const std::string& json);

    bool LoadFromFile(const std::wstring& filePath);
    bool SaveToFile(const std::wstring& filePath) const;

private:
    std::array<std::vector<KeyCombo>, static_cast<size_t>(PlayerAction::Count)> bindings_;
};

}  // namespace playeverything
