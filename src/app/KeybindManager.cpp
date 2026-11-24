#include "app/KeybindManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace playeverything {

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), size);
    return out;
}

std::string Trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string ToUpperAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

struct NamedKey {
    UINT vk;
    const char* name;
};

const NamedKey kNamedKeys[] = {
    {VK_SPACE, "Space"},
    {VK_RETURN, "Enter"},
    {VK_TAB, "Tab"},
    {VK_BACK, "Backspace"},
    {VK_ESCAPE, "Esc"},
    {VK_DELETE, "Delete"},
    {VK_INSERT, "Insert"},
    {VK_HOME, "Home"},
    {VK_END, "End"},
    {VK_PRIOR, "PageUp"},
    {VK_NEXT, "PageDown"},
    {VK_LEFT, "Left"},
    {VK_RIGHT, "Right"},
    {VK_UP, "Up"},
    {VK_DOWN, "Down"},
    {VK_OEM_PLUS, "Plus"},
    {VK_OEM_MINUS, "Minus"},
    {VK_OEM_COMMA, "Comma"},
    {VK_OEM_PERIOD, "Period"},
    {VK_OEM_2, "Slash"},
    {VK_OEM_1, "Semicolon"},
    {VK_OEM_7, "Quote"},
    {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"},
    {VK_OEM_5, "Backslash"},
    {VK_OEM_3, "Grave"},
    {VK_ADD, "NumAdd"},
    {VK_SUBTRACT, "NumSub"},
    {VK_MULTIPLY, "NumMul"},
    {VK_DIVIDE, "NumDiv"},
    {VK_DECIMAL, "NumDec"},
    {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"},
    {VK_MEDIA_STOP, "MediaStop"},
    {VK_MEDIA_NEXT_TRACK, "MediaNext"},
    {VK_MEDIA_PREV_TRACK, "MediaPrev"},
    {VK_VOLUME_MUTE, "VolumeMute"},
    {VK_VOLUME_UP, "VolumeUp"},
    {VK_VOLUME_DOWN, "VolumeDown"},
};

std::string KeyNameFromVk(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "Num" + std::to_string(vk - VK_NUMPAD0);
    }
    for (const auto& entry : kNamedKeys) {
        if (entry.vk == vk) {
            return entry.name;
        }
    }
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "VK_%u", vk);
    return std::string(buf);
}

std::optional<UINT> VkFromKeyName(const std::string& rawName) {
    std::string name = Trim(rawName);
    if (name.empty()) return std::nullopt;

    if (name.size() == 1) {
        unsigned char c = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<UINT>(c);
        }
    }

    std::string upper = ToUpperAscii(name);
    if (upper.size() >= 2 && upper[0] == 'F') {
        try {
            int n = std::stoi(upper.substr(1));
            if (n >= 1 && n <= 24) {
                return static_cast<UINT>(VK_F1 + (n - 1));
            }
        } catch (...) {
        }
    }
    if (upper.size() == 4 && upper.rfind("NUM", 0) == 0 && std::isdigit(static_cast<unsigned char>(upper[3]))) {
        return static_cast<UINT>(VK_NUMPAD0 + (upper[3] - '0'));
    }
    if (upper.rfind("VK_", 0) == 0) {
        try {
            unsigned long val = std::stoul(upper.substr(3));
            if (val > 0 && val < 256) {
                return static_cast<UINT>(val);
            }
        } catch (...) {
        }
    }

    for (const auto& entry : kNamedKeys) {
        if (ToUpperAscii(entry.name) == upper) {
            return entry.vk;
        }
    }
    return std::nullopt;
}

std::string EscapeJson(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 4);
    for (char c : in) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

class SimpleJsonReader {
public:
    explicit SimpleJsonReader(const std::string& src) : s_(src) {}

    void SkipWs() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    bool Consume(char c) {
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out) {
        SkipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool SkipValue() {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '"') {
            std::string dummy;
            return ParseString(dummy);
        }
        if (c == '{') {
            ++pos_;
            if (Consume('}')) return true;
            while (true) {
                std::string k;
                if (!ParseString(k) || !Consume(':') || !SkipValue()) return false;
                if (Consume('}')) return true;
                if (!Consume(',')) return false;
            }
        }
        if (c == '[') {
            ++pos_;
            if (Consume(']')) return true;
            while (true) {
                if (!SkipValue()) return false;
                if (Consume(']')) return true;
                if (!Consume(',')) return false;
            }
        }
        while (pos_ < s_.size() && s_[pos_] != ',' && s_[pos_] != '}' && s_[pos_] != ']' &&
               !std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
        return true;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;
};

}  // namespace

bool KeyCombo::IsModifierKey(UINT vk) {
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}

bool KeyCombo::IsValid() const {
    return vk != 0 && !IsModifierKey(vk);
}

std::string FormatKeyComboUtf8(const KeyCombo& combo) {
    if (!combo.IsValid()) return {};
    std::string out;
    if (combo.ctrl) out += "Ctrl+";
    if (combo.alt) out += "Alt+";
    if (combo.shift) out += "Shift+";
    out += KeyNameFromVk(combo.vk);
    return out;
}

std::wstring FormatKeyCombo(const KeyCombo& combo) {
    return Utf8ToWide(FormatKeyComboUtf8(combo));
}

std::optional<KeyCombo> ParseKeyComboUtf8(const std::string& text) {
    std::string trimmed = Trim(text);
    if (trimmed.empty()) return std::nullopt;

    KeyCombo combo;
    size_t start = 0;
    std::vector<std::string> parts;
    while (start <= trimmed.size()) {
        size_t plus = trimmed.find('+', start);
        if (plus == std::string::npos) {
            parts.push_back(Trim(trimmed.substr(start)));
            break;
        }
        parts.push_back(Trim(trimmed.substr(start, plus - start)));
        start = plus + 1;
    }

    if (parts.empty()) return std::nullopt;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string mod = ToUpperAscii(parts[i]);
        if (mod == "CTRL" || mod == "CONTROL") {
            combo.ctrl = true;
        } else if (mod == "ALT") {
            combo.alt = true;
        } else if (mod == "SHIFT") {
            combo.shift = true;
        } else {
            return std::nullopt;
        }
    }

    auto vk = VkFromKeyName(parts.back());
    if (!vk.has_value()) return std::nullopt;
    combo.vk = *vk;
    if (!combo.IsValid()) return std::nullopt;
    return combo;
}

KeybindManager::KeybindManager() {
    ResetDefaults();
}

void KeybindManager::ResetDefaults() {
    for (auto& list : bindings_) {
        list.clear();
    }
    bindings_[static_cast<size_t>(PlayerAction::PlayPause)] = {
        KeyCombo{VK_SPACE, false, false, false},
        KeyCombo{VK_MEDIA_PLAY_PAUSE, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::Restart)] = {
        KeyCombo{'R', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::SeekBack15)] = {
        KeyCombo{VK_LEFT, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::SeekForward15)] = {
        KeyCombo{VK_RIGHT, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::ToggleLoop)] = {
        KeyCombo{'L', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::ToggleShuffle)] = {
        KeyCombo{'S', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::VolumeUp)] = {
        KeyCombo{VK_UP, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::VolumeDown)] = {
        KeyCombo{VK_DOWN, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::ToggleMute)] = {
        KeyCombo{'M', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::ToggleFullscreen)] = {
        KeyCombo{'F', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::OpenFile)] = {
        KeyCombo{'O', true, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::NextTrack)] = {
        KeyCombo{VK_MEDIA_NEXT_TRACK, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::PrevTrack)] = {
        KeyCombo{VK_MEDIA_PREV_TRACK, false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::TogglePlaylist)] = {
        KeyCombo{'P', false, false, false},
    };
    bindings_[static_cast<size_t>(PlayerAction::ShowInfo)] = {
        KeyCombo{'I', false, false, false},
    };
}

const std::vector<KeyCombo>& KeybindManager::GetBindings(PlayerAction action) const {
    return bindings_[static_cast<size_t>(action)];
}

std::wstring KeybindManager::GetBindingsSummary(PlayerAction action) const {
    const auto& list = GetBindings(action);
    if (list.empty()) return L"(None)";
    std::wstring out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) out += L", ";
        out += FormatKeyCombo(list[i]);
    }
    return out;
}

std::wstring KeybindManager::GetPrimaryShortcut(PlayerAction action) const {
    const auto& list = GetBindings(action);
    if (list.empty()) return {};
    return FormatKeyCombo(list.front());
}

std::optional<PlayerAction> KeybindManager::FindAction(const KeyCombo& combo) const {
    if (!combo.IsValid()) return std::nullopt;
    for (PlayerAction action : AllActions()) {
        const auto& list = bindings_[static_cast<size_t>(action)];
        if (std::find(list.begin(), list.end(), combo) != list.end()) {
            return action;
        }
    }
    return std::nullopt;
}

bool KeybindManager::AddBinding(PlayerAction action, const KeyCombo& combo) {
    if (!combo.IsValid()) return false;
    auto& list = bindings_[static_cast<size_t>(action)];
    if (std::find(list.begin(), list.end(), combo) != list.end()) {
        return false;
    }
    list.push_back(combo);
    return true;
}

bool KeybindManager::ReplaceBinding(PlayerAction action, size_t index, const KeyCombo& combo) {
    if (!combo.IsValid()) return false;
    auto& list = bindings_[static_cast<size_t>(action)];
    if (index >= list.size()) return false;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i != index && list[i] == combo) {
            return false;
        }
    }
    list[index] = combo;
    return true;
}

bool KeybindManager::RemoveBinding(PlayerAction action, size_t index) {
    auto& list = bindings_[static_cast<size_t>(action)];
    if (index >= list.size()) return false;
    list.erase(list.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

void KeybindManager::RemoveComboEverywhere(const KeyCombo& combo) {
    for (auto& list : bindings_) {
        list.erase(std::remove(list.begin(), list.end(), combo), list.end());
    }
}

std::string KeybindManager::SerializeJson() const {
    std::ostringstream oss;
    oss << "{\n  \"version\": 1,\n  \"bindings\": {\n";
    const auto& actions = AllActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        PlayerAction action = actions[i];
        oss << "    \"" << EscapeJson(ActionId(action)) << "\": [";
        const auto& list = bindings_[static_cast<size_t>(action)];
        for (size_t j = 0; j < list.size(); ++j) {
            if (j > 0) oss << ", ";
            oss << "\"" << EscapeJson(FormatKeyComboUtf8(list[j])) << "\"";
        }
        oss << "]";
        if (i + 1 < actions.size()) oss << ",";
        oss << "\n";
    }
    oss << "  }\n}\n";
    return oss.str();
}

bool KeybindManager::DeserializeJson(const std::string& json) {
    SimpleJsonReader reader(json);
    if (!reader.Consume('{')) return false;

    std::array<std::vector<KeyCombo>, static_cast<size_t>(PlayerAction::Count)> parsed;
    bool foundBindings = false;

    if (!reader.Consume('}')) {
        while (true) {
            std::string key;
            if (!reader.ParseString(key) || !reader.Consume(':')) return false;
            if (key == "bindings") {
                foundBindings = true;
                if (!reader.Consume('{')) return false;
                if (!reader.Consume('}')) {
                    while (true) {
                        std::string actKey;
                        if (!reader.ParseString(actKey) || !reader.Consume(':') || !reader.Consume('[')) {
                            return false;
                        }
                        auto actionOpt = ActionFromId(actKey);
                        if (!reader.Consume(']')) {
                            while (true) {
                                std::string comboStr;
                                if (!reader.ParseString(comboStr)) return false;
                                if (actionOpt.has_value()) {
                                    auto combo = ParseKeyComboUtf8(comboStr);
                                    if (combo.has_value()) {
                                        auto& vec = parsed[static_cast<size_t>(*actionOpt)];
                                        if (std::find(vec.begin(), vec.end(), *combo) == vec.end()) {
                                            vec.push_back(*combo);
                                        }
                                    }
                                }
                                if (reader.Consume(']')) break;
                                if (!reader.Consume(',')) return false;
                            }
                        }
                        if (reader.Consume('}')) break;
                        if (!reader.Consume(',')) return false;
                    }
                }
            } else {
                if (!reader.SkipValue()) return false;
            }
            if (reader.Consume('}')) break;
            if (!reader.Consume(',')) return false;
        }
    }

    if (!foundBindings) return false;
    bindings_ = std::move(parsed);
    return true;
}

bool KeybindManager::LoadFromFile(const std::wstring& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return DeserializeJson(oss.str());
}

bool KeybindManager::SaveToFile(const std::wstring& filePath) const {
    std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    std::string content = SerializeJson();
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return ofs.good();
}

}  // namespace playeverything
