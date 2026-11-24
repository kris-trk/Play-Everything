#include "app/AppActions.h"
#include "app/KeybindManager.h"
#include "app/PlaylistManager.h"
#include "app/TimeFormat.h"
#include "media/MediaEngine.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace playeverything;

int gFailures = 0;

void Expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailures;
    } else {
        std::cout << "[PASS] " << msg << "\n";
    }
}

void TestTimeFormat() {
    Expect(FormatTimestamp(0.0) == L"00:00", "FormatTimestamp(0) == 00:00");
    Expect(FormatTimestamp(59.4) == L"00:59", "FormatTimestamp(59.4) == 00:59");
    Expect(FormatTimestamp(65.0) == L"01:05", "FormatTimestamp(65) == 01:05");
    Expect(FormatTimestamp(3661.0) == L"1:01:01", "FormatTimestamp(3661) == 1:01:01");
    Expect(FormatTimestamp(65.0, true) == L"0:01:05", "FormatTimestamp(65, forceHours) == 0:01:05");
    Expect(FormatTimestamp(-10.0) == L"00:00", "FormatTimestamp(-10) clamps to 00:00");
    Expect(FormatTimestamp(std::numeric_limits<double>::quiet_NaN()) == L"00:00", "FormatTimestamp(NaN) == 00:00");

    Expect(ClampSeekTarget(-5.0, 100.0) == 0.0, "ClampSeekTarget(-5, 100) == 0");
    Expect(ClampSeekTarget(120.0, 100.0) == 100.0, "ClampSeekTarget(120, 100) == 100");
    Expect(ClampSeekTarget(42.5, 100.0) == 42.5, "ClampSeekTarget(42.5, 100) == 42.5");
}

void TestKeybindManager() {
    KeybindManager mgr;

    // Defaults
    auto actSpace = mgr.FindAction(KeyCombo{VK_SPACE, false, false, false});
    Expect(actSpace.has_value() && *actSpace == PlayerAction::PlayPause, "Default Space -> PlayPause");

    auto actLeft = mgr.FindAction(KeyCombo{VK_LEFT, false, false, false});
    Expect(actLeft.has_value() && *actLeft == PlayerAction::SeekBack15, "Default Left -> SeekBack15");

    auto actRight = mgr.FindAction(KeyCombo{VK_RIGHT, false, false, false});
    Expect(actRight.has_value() && *actRight == PlayerAction::SeekForward15, "Default Right -> SeekForward15");

    auto actLoop = mgr.FindAction(KeyCombo{'L', false, false, false});
    Expect(actLoop.has_value() && *actLoop == PlayerAction::ToggleLoop, "Default L -> ToggleLoop");

    // Add second keybind to SeekForward15
    KeyCombo ctrlRight{VK_RIGHT, true, false, false};
    Expect(mgr.AddBinding(PlayerAction::SeekForward15, ctrlRight), "AddBinding Ctrl+Right to SeekForward15");
    Expect(!mgr.AddBinding(PlayerAction::SeekForward15, ctrlRight), "Duplicate AddBinding rejected");
    Expect(mgr.GetBindings(PlayerAction::SeekForward15).size() == 2, "SeekForward15 now has 2 bindings");

    // Replace binding
    KeyCombo altP{'P', false, true, false};
    Expect(mgr.AddBinding(PlayerAction::Play, altP), "AddBinding Alt+P to Play");
    KeyCombo shiftP{'P', false, false, true};
    Expect(mgr.ReplaceBinding(PlayerAction::Play, 0, shiftP), "ReplaceBinding Play[0] -> Shift+P");
    Expect(!mgr.FindAction(altP).has_value(), "Old binding Alt+P removed after replace");
    Expect(mgr.FindAction(shiftP) == PlayerAction::Play, "New binding Shift+P resolves to Play");

    // Remove binding
    Expect(mgr.RemoveBinding(PlayerAction::SeekForward15, 0), "RemoveBinding SeekForward15[0]");
    Expect(mgr.GetBindings(PlayerAction::SeekForward15).size() == 1, "SeekForward15 now has 1 binding");

    // Conflict cleanup
    mgr.RemoveComboEverywhere(KeyCombo{'R', false, false, false});
    Expect(!mgr.FindAction(KeyCombo{'R', false, false, false}).has_value(), "RemoveComboEverywhere clears R");

    // JSON persistence round-trip
    std::filesystem::path tmpJson = std::filesystem::temp_directory_path() / L"playeverything_test_keybinds.json";
    Expect(mgr.SaveToFile(tmpJson.wstring()), "SaveToFile writes keybinds.json");

    KeybindManager loaded;
    loaded.ResetDefaults();
    Expect(loaded.LoadFromFile(tmpJson.wstring()), "LoadFromFile reads keybinds.json");
    Expect(loaded.FindAction(shiftP) == PlayerAction::Play, "Loaded JSON preserves Shift+P -> Play");
    Expect(loaded.FindAction(ctrlRight) == PlayerAction::SeekForward15, "Loaded JSON preserves Ctrl+Right -> SeekForward15");
    Expect(!loaded.FindAction(KeyCombo{'R', false, false, false}).has_value(), "Loaded JSON preserves removed R");

    std::error_code ec;
    std::filesystem::remove(tmpJson, ec);

    // Reset defaults
    loaded.ResetDefaults();
    Expect(loaded.FindAction(KeyCombo{'R', false, false, false}) == PlayerAction::Restart, "ResetDefaults restores R -> Restart");
}

bool GenerateTestWav(const std::filesystem::path& path, int durationSeconds) {
    const uint32_t sampleRate = 8000;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t numSamples = sampleRate * static_cast<uint32_t>(durationSeconds);
    const uint32_t dataSize = numSamples * channels * (bitsPerSample / 8);
    const uint32_t riffSize = 36 + dataSize;
    const uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    const uint16_t blockAlign = channels * (bitsPerSample / 8);

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;

    auto writeU32 = [&](uint32_t v) { ofs.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { ofs.write(reinterpret_cast<const char*>(&v), 2); };

    ofs.write("RIFF", 4);
    writeU32(riffSize);
    ofs.write("WAVE", 4);
    ofs.write("fmt ", 4);
    writeU32(16);
    writeU16(1);  // PCM
    writeU16(channels);
    writeU32(sampleRate);
    writeU32(byteRate);
    writeU16(blockAlign);
    writeU16(bitsPerSample);
    ofs.write("data", 4);
    writeU32(dataSize);

    std::vector<int16_t> samples(numSamples);
    for (uint32_t i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        samples[i] = static_cast<int16_t>(4000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
    }
    ofs.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataSize));
    return ofs.good();
}

template <typename Pred>
bool WaitUntil(MediaEngine& engine, Pred pred, int timeoutMs = 4000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        engine.ProcessEvents(0.05);
        if (pred()) return true;
    }
    return pred();
}

void TestHeadlessMediaEngine() {
    std::filesystem::path wavPath = std::filesystem::temp_directory_path() / L"playeverything_test_35s.wav";
    Expect(GenerateTestWav(wavPath, 35), "Generated 35-second test WAV audio file");

    MediaEngine engine;
    Expect(engine.Initialize(nullptr, nullptr, 0, true), "MediaEngine initializes headless libmpv");
    Expect(engine.OpenFile(wavPath.wstring()), "MediaEngine opens WAV file");

    bool loaded = WaitUntil(engine, [&]() {
        return engine.State().loaded && engine.State().duration >= 34.0;
    });
    Expect(loaded, "libmpv loads WAV and reports ~35s duration");

    // Pause
    engine.Pause();
    Expect(WaitUntil(engine, [&]() { return engine.State().paused; }), "Pause sets paused=true");

    // Forward 15s
    double beforeFwd = engine.State().timePos;
    engine.SeekRelative(15.0);
    bool fwdOk = WaitUntil(engine, [&]() {
        return engine.State().timePos >= beforeFwd + 14.0;
    });
    Expect(fwdOk, "+15s seek advances playback position by ~15s");

    // Backward 15s
    engine.SeekRelative(-15.0);
    bool backOk = WaitUntil(engine, [&]() {
        return engine.State().timePos <= beforeFwd + 1.5;
    });
    Expect(backOk, "-15s seek rewinds playback position by ~15s");

    // Seek to 20s then Restart -> jumps to ~0 and unpauses
    engine.SeekAbsolute(20.0, true);
    Expect(WaitUntil(engine, [&]() { return engine.State().timePos >= 19.0; }), "SeekAbsolute(20s) reaches ~20s");

    engine.Restart();
    bool restartOk = WaitUntil(engine, [&]() {
        return !engine.State().paused && engine.State().timePos < 2.0;
    });
    Expect(restartOk, "Restart jumps back to ~0s and resumes playback");

    // Loop toggle
    engine.SetLoop(true);
    Expect(WaitUntil(engine, [&]() { return engine.State().loopEnabled; }), "SetLoop(true) enables loop-file=inf");
    engine.ToggleLoop();
    Expect(WaitUntil(engine, [&]() { return !engine.State().loopEnabled; }), "ToggleLoop disables loop-file");

    // Speed adjustment (presets + custom)
    engine.SetSpeed(2.0);
    Expect(WaitUntil(engine, [&]() { return std::abs(engine.State().speed - 2.0) < 0.01; }), "SetSpeed(2.0) sets speed=2.0x");
    engine.SetSpeed(0.25);
    Expect(WaitUntil(engine, [&]() { return std::abs(engine.State().speed - 0.25) < 0.01; }), "SetSpeed(0.25) sets speed=0.25x");
    engine.SetSpeed(1.45);
    Expect(WaitUntil(engine, [&]() { return std::abs(engine.State().speed - 1.45) < 0.01; }), "SetSpeed(1.45) sets custom speed=1.45x");
    engine.SetSpeed(1.0);
    Expect(WaitUntil(engine, [&]() { return std::abs(engine.State().speed - 1.0) < 0.01; }), "SetSpeed(1.0) resets speed=1.0x");

    // Media info query
    auto info = engine.QueryMediaInfo();
    bool hasDuration = false;
    bool hasSampleRate = false;
    for (const auto& row : info) {
        if (row.key == L"Duration") hasDuration = true;
        if (row.key == L"Sample Rate") hasSampleRate = true;
    }
    Expect(hasDuration && hasSampleRate, "QueryMediaInfo returns Duration and Sample Rate");

    engine.Shutdown();

    std::error_code ec;
    std::filesystem::remove(wavPath, ec);
}

void TestPlaylistManager() {
    PlaylistManager pl;
    Expect(pl.Empty(), "Playlist starts empty");

    pl.AddFiles({L"C:\\media\\a.mp4", L"C:\\media\\b.mp3", L"C:\\media\\c.mkv"});
    Expect(pl.Size() == 3, "AddFiles adds 3 items");
    Expect(pl.SetCurrentIndex(0), "Select index 0");
    Expect(pl.HasNext() && !pl.HasPrevious(), "At start: HasNext=true, HasPrevious=false");

    auto next1 = pl.Next();
    Expect(next1.has_value() && *next1 == L"C:\\media\\b.mp3", "Next() advances to b.mp3");
    Expect(pl.CurrentIndex() == 1, "CurrentIndex is 1");

    Expect(pl.MoveUp(1), "MoveUp(1) swaps b.mp3 to index 0");
    Expect(pl.CurrentIndex() == 0 && pl.Items()[0].displayName == L"b.mp3", "CurrentIndex follows moved active item");

    Expect(pl.MoveDown(0), "MoveDown(0) moves b.mp3 back to index 1");
    Expect(pl.CurrentIndex() == 1, "CurrentIndex follows MoveDown");

    auto prev = pl.Previous();
    Expect(prev.has_value() && *prev == L"C:\\media\\a.mp4", "Previous() returns to a.mp4");

    Expect(pl.RemoveAt(0), "RemoveAt(0) removes first item");
    Expect(pl.Size() == 2 && pl.CurrentIndex() == 0, "Playlist size is 2 and CurrentIndex stays valid");

    // Shuffle mode
    pl.AddFiles({L"C:\\media\\d.flac", L"C:\\media\\e.wav"});
    pl.SetCurrentIndex(0);
    Expect(!pl.IsShuffleEnabled(), "Shuffle starts off");
    pl.ToggleShuffle();
    Expect(pl.IsShuffleEnabled(), "ToggleShuffle turns shuffle on");
    Expect(pl.HasNext(), "Shuffle HasNext=true when >1 items");
    auto s1 = pl.Next();
    auto s2 = pl.Next();
    Expect(s1.has_value() && s2.has_value() && *s1 != *s2, "Shuffle Next() visits distinct tracks without immediate repeat");
    Expect(pl.HasPrevious(), "Shuffle HasPrevious=true after advancing");
    auto sprev = pl.Previous();
    Expect(sprev.has_value() && *sprev == *s1, "Shuffle Previous() steps back through shuffle history");
    pl.SetShuffle(false);
    Expect(!pl.IsShuffleEnabled(), "SetShuffle(false) disables shuffle");

    pl.Clear();
    Expect(pl.Empty() && pl.CurrentIndex() == -1, "Clear resets playlist");
}

}  // namespace

int main() {
    std::cout << "=== Play Everything Automated Tests ===\n";
    TestTimeFormat();
    TestKeybindManager();
    TestPlaylistManager();
    TestHeadlessMediaEngine();

    if (gFailures > 0) {
        std::cerr << "\nFAILED: " << gFailures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "\nALL TESTS PASSED.\n";
    return 0;
}
