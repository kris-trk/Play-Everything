#include "setup/WindowsIntegration.h"

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <filesystem>

namespace playeverything::setup {

namespace {

constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlayEverything";
constexpr wchar_t kAppRegKey[] = L"Software\\Classes\\Applications\\PlayEverything.exe";
constexpr wchar_t kProgIdName[] = L"PlayEverything.Media";
constexpr wchar_t kProgIdKey[] = L"Software\\Classes\\PlayEverything.Media";
constexpr wchar_t kCapabilitiesRoot[] = L"Software\\PlayEverything";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\PlayEverything\\Capabilities";
constexpr wchar_t kRegisteredAppsKey[] = L"Software\\RegisteredApplications";

bool RegWriteSz(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
    LONG res = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), bytes);
    RegCloseKey(key);
    return res == ERROR_SUCCESS;
}

bool RegWriteDword(HKEY root, const std::wstring& subKey, const wchar_t* valueName, DWORD data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG res = RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data));
    RegCloseKey(key);
    return res == ERROR_SUCCESS;
}

std::wstring RegReadSz(HKEY root, const std::wstring& subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0;
    DWORD size = 0;
    std::wstring out;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size >= sizeof(wchar_t)) {
        out.resize(size / sizeof(wchar_t));
        if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(out.data()), &size) == ERROR_SUCCESS) {
            while (!out.empty() && out.back() == L'\0') out.pop_back();
        } else {
            out.clear();
        }
    }
    RegCloseKey(key);
    return out;
}

void RegDeleteValueSafe(HKEY root, const std::wstring& subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey.c_str(), 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, valueName);
        RegCloseKey(key);
    }
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}

}  // namespace

const std::vector<std::wstring>& SupportedMediaExtensions() {
    static const std::vector<std::wstring> kExts = {
        L".mp4", L".mov", L".mkv", L".avi", L".webm", L".wmv", L".flv", L".m4v",
        L".ts", L".m2ts", L".mpg", L".mpeg", L".3gp", L".ogv", L".mts", L".vob", L".asf",
        L".mp3", L".flac", L".wav", L".aac", L".m4a", L".ogg", L".opus", L".wma",
        L".alac", L".ape", L".ac3", L".dts", L".wv", L".aiff", L".aif", L".mka",
    };
    return kExts;
}

std::wstring GetDefaultInstallDir() {
    std::wstring localAppData = KnownFolder(FOLDERID_LocalAppData);
    if (localAppData.empty()) return L"C:\\Play Everything";
    return (std::filesystem::path(localAppData) / L"Programs" / L"Play Everything").wstring();
}

std::wstring GetDesktopShortcutPath() {
    std::wstring desktop = KnownFolder(FOLDERID_Desktop);
    if (desktop.empty()) return {};
    return (std::filesystem::path(desktop) / L"Play Everything.lnk").wstring();
}

std::wstring GetStartMenuFolderPath() {
    std::wstring programs = KnownFolder(FOLDERID_Programs);
    if (programs.empty()) return {};
    return (std::filesystem::path(programs) / L"Play Everything").wstring();
}

bool CreateShortcut(const std::wstring& lnkPath,
                    const std::wstring& targetPath,
                    const std::wstring& arguments,
                    const std::wstring& workingDir,
                    const std::wstring& description,
                    const std::wstring& iconPath) {
    if (lnkPath.empty() || targetPath.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(lnkPath).parent_path(), ec);

    Microsoft::WRL::ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return false;
    }

    link->SetPath(targetPath.c_str());
    if (!arguments.empty()) link->SetArguments(arguments.c_str());
    if (!workingDir.empty()) link->SetWorkingDirectory(workingDir.c_str());
    if (!description.empty()) link->SetDescription(description.c_str());
    if (!iconPath.empty()) link->SetIconLocation(iconPath.c_str(), 0);

    Microsoft::WRL::ComPtr<IPersistFile> pf;
    if (FAILED(link.As(&pf))) return false;
    return SUCCEEDED(pf->Save(lnkPath.c_str(), TRUE));
}

void CloseRunningPlayEverything() {
    // Gracefully ask any running PlayEverything windows to close
    HWND hwnd = FindWindowW(L"PlayEverythingMainWindow", nullptr);
    int attempts = 0;
    while (hwnd && attempts < 10) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        Sleep(250);
        hwnd = FindWindowW(L"PlayEverythingMainWindow", nullptr);
        ++attempts;
    }

    // Ensure no lingering PlayEverything.exe processes hold DLL/EXE locks
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"PlayEverything.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (WaitForSingleObject(hProc, 800) != WAIT_OBJECT_0) {
                        TerminateProcess(hProc, 0);
                        WaitForSingleObject(hProc, 1500);
                    }
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool RegisterWindowsIntegration(const std::wstring& installDir,
                                bool registerMediaAssociations,
                                bool createdDesktopShortcut,
                                bool createdStartMenu) {
    std::filesystem::path dir(installDir);
    std::wstring exePath = (dir / L"PlayEverything.exe").wstring();
    std::wstring uninstPath = (dir / L"Uninstall.exe").wstring();
    std::wstring iconLoc = exePath + L",0";
    std::wstring openCmd = L"\"" + exePath + L"\" \"%1\"";

    if (registerMediaAssociations) {
        // 1. Applications\PlayEverything.exe (Windows "Open with" app identity)
        RegWriteSz(HKEY_CURRENT_USER, kAppRegKey, L"FriendlyAppName", L"Play Everything");
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kAppRegKey) + L"\\DefaultIcon", nullptr, iconLoc);
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kAppRegKey) + L"\\shell\\open", L"FriendlyAppName", L"Play Everything");
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kAppRegKey) + L"\\shell\\open\\command", nullptr, openCmd);

        std::wstring supportedKey = std::wstring(kAppRegKey) + L"\\SupportedTypes";
        for (const auto& ext : SupportedMediaExtensions()) {
            RegWriteSz(HKEY_CURRENT_USER, supportedKey, ext.c_str(), L"");
        }

        // 2. ProgID PlayEverything.Media + per-extension OpenWithProgids
        RegWriteSz(HKEY_CURRENT_USER, kProgIdKey, nullptr, L"Play Everything Media File");
        RegWriteSz(HKEY_CURRENT_USER, kProgIdKey, L"FriendlyTypeName", L"Play Everything Media File");
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kProgIdKey) + L"\\DefaultIcon", nullptr, iconLoc);
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kProgIdKey) + L"\\shell\\open", L"FriendlyAppName", L"Play Everything");
        RegWriteSz(HKEY_CURRENT_USER, std::wstring(kProgIdKey) + L"\\shell\\open\\command", nullptr, openCmd);

        for (const auto& ext : SupportedMediaExtensions()) {
            std::wstring owpKey = L"Software\\Classes\\" + ext + L"\\OpenWithProgids";
            RegWriteSz(HKEY_CURRENT_USER, owpKey, kProgIdName, L"");
        }

        // 3. RegisteredApplications (Windows Settings -> Default apps)
        RegWriteSz(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationName", L"Play Everything");
        RegWriteSz(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationDescription",
                   L"Fast universal audio and video player supporting all codecs.");
        RegWriteSz(HKEY_CURRENT_USER, kCapabilitiesKey, L"ApplicationIcon", iconLoc);

        std::wstring faKey = std::wstring(kCapabilitiesKey) + L"\\FileAssociations";
        for (const auto& ext : SupportedMediaExtensions()) {
            RegWriteSz(HKEY_CURRENT_USER, faKey, ext.c_str(), kProgIdName);
        }
        RegWriteSz(HKEY_CURRENT_USER, kRegisteredAppsKey, L"PlayEverything", kCapabilitiesKey);
    }

    // 4. Add/Remove Programs (Windows Settings -> Apps -> Installed apps)
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"DisplayName", L"Play Everything");
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"DisplayIcon", iconLoc);
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", L"1.2.0");
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"Publisher", L"Play Everything");
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", installDir);
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"UninstallString", L"\"" + uninstPath + L"\"");
    RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"QuietUninstallString", L"\"" + uninstPath + L"\" /S");
    RegWriteDword(HKEY_CURRENT_USER, kUninstallKey, L"NoModify", 1);
    RegWriteDword(HKEY_CURRENT_USER, kUninstallKey, L"NoRepair", 1);
    RegWriteDword(HKEY_CURRENT_USER, kUninstallKey, L"EstimatedSize", 122000);  // ~122 MB in KB

    if (createdDesktopShortcut) {
        RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"DesktopShortcutPath", GetDesktopShortcutPath());
    }
    if (createdStartMenu) {
        RegWriteSz(HKEY_CURRENT_USER, kUninstallKey, L"StartMenuDir", GetStartMenuFolderPath());
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

void UnregisterAllWindowsIntegration(std::wstring* outInstallDir) {
    std::wstring savedInstallDir = RegReadSz(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
    std::wstring savedDesktopLnk = RegReadSz(HKEY_CURRENT_USER, kUninstallKey, L"DesktopShortcutPath");
    std::wstring savedStartDir = RegReadSz(HKEY_CURRENT_USER, kUninstallKey, L"StartMenuDir");

    if (outInstallDir && !savedInstallDir.empty()) {
        *outInstallDir = savedInstallDir;
    }

    // Remove Desktop shortcut (both saved path and standard Desktop path)
    std::error_code ec;
    if (!savedDesktopLnk.empty()) {
        std::filesystem::remove(savedDesktopLnk, ec);
    }
    std::wstring stdDesktop = GetDesktopShortcutPath();
    if (!stdDesktop.empty()) {
        std::filesystem::remove(stdDesktop, ec);
    }

    // Remove Start Menu folder
    if (!savedStartDir.empty()) {
        std::filesystem::remove_all(savedStartDir, ec);
    }
    std::wstring stdStart = GetStartMenuFolderPath();
    if (!stdStart.empty()) {
        std::filesystem::remove_all(stdStart, ec);
    }

    // Remove OpenWithProgids entries without disturbing other apps
    for (const auto& ext : SupportedMediaExtensions()) {
        std::wstring owpKey = L"Software\\Classes\\" + ext + L"\\OpenWithProgids";
        RegDeleteValueSafe(HKEY_CURRENT_USER, owpKey, kProgIdName);
    }

    // Delete all Play Everything registry trees
    RegDeleteTreeW(HKEY_CURRENT_USER, kAppRegKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kProgIdKey);
    RegDeleteValueSafe(HKEY_CURRENT_USER, kRegisteredAppsKey, L"PlayEverything");
    RegDeleteTreeW(HKEY_CURRENT_USER, kCapabilitiesRoot);
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace playeverything::setup
