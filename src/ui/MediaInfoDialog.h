#pragma once

#include "media/MediaEngine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <vector>

namespace playeverything {

void ShowMediaInfoDialog(HWND owner, HINSTANCE hInstance, const std::vector<MediaInfoEntry>& entries);

}  // namespace playeverything
