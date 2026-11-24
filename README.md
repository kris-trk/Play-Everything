# Play Everything

A fast, lightweight, native Windows 11 audio & video player built in C++20 (Win32, Direct2D/DirectWrite, and `libmpv`) that plays virtually any video or audio codec at full native quality and uncapped framerate.

## Features

- **Universal Codec Support (`libmpv` / FFmpeg)**: Plays MP4, MOV, MKV, AVI, WebM, WMV, FLV, TS/M2TS, HEVC/H.265, H.264, AV1, VP9, ProRes, MP3, FLAC, WAV, AAC, M4A, OGG, Opus, ALAC, WMA, APE, AC3, DTS, and more with hardware acceleration (`D3D11VA` / software fallback) and WASAPI audio.
- **Uncapped Native FPS & Highest Quality**: Renders at the exact native framerate and full native resolution/quality of the source file (`profile=high-quality`, `video-sync=audio`).
- **Simple Controls & Clean UI**:
  - Play, Pause, Restart, Loop, Shuffle, Backward 15s (`-15s`), Forward 15s (`+15s`), Volume + Mute, and Fullscreen (double-click or `F`, `Esc` to exit).
  - **Timeline Hover Preview**: Floating video frame thumbnail preview (and timestamp pill for audio) when hovering or scrubbing the timeline.
  - **Playback Speed**: `0.25x`, `0.5x`, `0.75x`, `1x`, `2x`, `3x`, plus any **Custom** speed (`0.1x`–`16x`).
  - **Playlist Panel**: Docked side drawer with multi-file open/drag-and-drop, track reordering, auto-advance, and non-repeating Shuffle mode.
  - **Media Info**: Live resolution, aspect ratio, framerate, bitrate, codecs, pixel format, hardware decoder, and container/stream metadata with one-click Copy All.
  - **Customizable Keybinds**: Add, change, remove, and reset shortcuts in **Settings → Keybinds...** (saved to `keybinds.json`).
- **Native Windows Installer & Complete Uninstaller**:
  - `PlayEverythingSetup.exe` supports custom installation paths, Desktop & Start Menu shortcuts, and Windows **"Open with"** / Default Apps media player registration across 33 video & audio formats.
  - `Uninstall.exe` (also accessible via **Settings → Uninstall Play Everything...** or Windows Settings → Installed apps) cleanly removes all files, settings, shortcuts, and registry entries.

## Building from Source (Windows x64)

### Prerequisites
- Windows 10/11 (x64)
- Visual Studio 2022/2026 with **Desktop development with C++** (MSVC + CMake + Windows 10/11 SDK)

### Steps

1. **Fetch the `libmpv` Windows SDK** (`libmpv-2.dll`, headers, and import library):
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\fetch_libmpv.ps1
   ```

2. **Configure & build Release x64**:
   ```powershell
   cmake -B build -A x64
   cmake --build build --config Release
   ```
   *(If using Visual Studio 2026's bundled CMake, run `cmake.exe` from Visual Studio's `CommonExtensions\Microsoft\CMake\CMake\bin` with `-G "Visual Studio 18 2026" -A x64`.)*

3. **Run automated unit & headless `libmpv` tests**:
   ```powershell
   ctest --test-dir build -C Release --output-on-failure
   ```

### Output Artifacts
- Portable app: `dist/PlayEverything/PlayEverything.exe` (+ `libmpv-2.dll`, `Uninstall.exe`)
- Single-file installer: `dist/PlayEverythingSetup.exe`
