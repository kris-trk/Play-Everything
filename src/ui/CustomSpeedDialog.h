#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <optional>

namespace playeverything {

// Opens a modal dialog prompting for a custom playback speed (0.1x to 16.0x).
std::optional<double> ShowCustomSpeedDialog(HWND owner, HINSTANCE hInstance, double currentSpeed);

}  // namespace playeverything
