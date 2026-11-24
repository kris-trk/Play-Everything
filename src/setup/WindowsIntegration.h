#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace playeverything::setup {

const std::vector<std::wstring>& SupportedMediaExtensions();

std::wstring GetDefaultInstallDir();
std::wstring GetDesktopShortcutPath();
std::wstring GetStartMenuFolderPath();

bool CreateShortcut(const std::wstring& lnkPath,
                    const std::wstring& targetPath,
                    const std::wstring& arguments,
                    const std::wstring& workingDir,
                    const std::wstring& description,
                    const std::wstring& iconPath);

void CloseRunningPlayEverything();

bool RegisterWindowsIntegration(const std::wstring& installDir,
                                bool registerMediaAssociations,
                                bool createdDesktopShortcut,
                                bool createdStartMenu);

void UnregisterAllWindowsIntegration(std::wstring* outInstallDir = nullptr);

}  // namespace playeverything::setup
