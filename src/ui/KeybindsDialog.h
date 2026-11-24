#pragma once

#include "app/KeybindManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace playeverything {

// Opens the modal Keybinds editor dialog.
// Returns true if the user clicked Save (updating keybinds in-place), false if cancelled.
bool ShowKeybindsDialog(HWND owner, HINSTANCE hInstance, KeybindManager& keybinds);

}  // namespace playeverything
