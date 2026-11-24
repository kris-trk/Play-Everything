cmake_minimum_required(VERSION 3.24)
project(PlayEverything VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBMPV_DIR "${CMAKE_SOURCE_DIR}/third_party/libmpv")
set(LIBMPV_INCLUDE_DIR "${LIBMPV_DIR}/include")
set(LIBMPV_LIB "${LIBMPV_DIR}/mpv.lib")
set(LIBMPV_DLL "${LIBMPV_DIR}/libmpv-2.dll")

if(NOT EXISTS "${LIBMPV_INCLUDE_DIR}/mpv/client.h" OR NOT EXISTS "${LIBMPV_LIB}" OR NOT EXISTS "${LIBMPV_DLL}")
    message(FATAL_ERROR "libmpv SDK not found in ${LIBMPV_DIR}. Run scripts/fetch_libmpv.ps1 first.")
endif()

add_library(playeverything_core STATIC
    src/app/AppActions.h
    src/app/TimeFormat.h
    src/app/TimeFormat.cpp
    src/app/KeybindManager.h
    src/app/KeybindManager.cpp
    src/app/PlaylistManager.h
    src/app/PlaylistManager.cpp
    src/media/MediaEngine.h
    src/media/MediaEngine.cpp
    src/media/ThumbnailProvider.h
    src/media/ThumbnailProvider.cpp
)

target_include_directories(playeverything_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${LIBMPV_INCLUDE_DIR}"
)

target_compile_definitions(playeverything_core PUBLIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(playeverything_core PUBLIC
    "${LIBMPV_LIB}"
)

if(MSVC)
    target_compile_options(playeverything_core PRIVATE /W4 /utf-8)
endif()

# Main Windows GUI application
add_executable(PlayEverything WIN32
    src/main.cpp
    src/ui/Resource.h
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TransportBar.h
    src/ui/TransportBar.cpp
    src/ui/EmptyAudioView.h
    src/ui/EmptyAudioView.cpp
    src/ui/PlaylistPanel.h
    src/ui/PlaylistPanel.cpp
    src/ui/TimelinePreviewPopup.h
    src/ui/TimelinePreviewPopup.cpp
    src/ui/KeybindsDialog.h
    src/ui/KeybindsDialog.cpp
    src/ui/CustomSpeedDialog.h
    src/ui/CustomSpeedDialog.cpp
    src/ui/MediaInfoDialog.h
    src/ui/MediaInfoDialog.cpp
    src/ui/App.rc
    src/ui/App.manifest
)

target_link_libraries(PlayEverything PRIVATE
    playeverything_core
    d2d1
    dwrite
    dwmapi
    comctl32
    ole32
    shell32
    shlwapi
    delayimp
)

if(MSVC)
    target_compile_options(PlayEverything PRIVATE /W4 /utf-8)
    target_link_options(PlayEverything PRIVATE /DELAYLOAD:libmpv-2.dll)
endif()

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist/PlayEverything")

add_custom_command(TARGET PlayEverything POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBMPV_DLL}" "$<TARGET_FILE_DIR:PlayEverything>/libmpv-2.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PlayEverything>" "${DIST_DIR}/PlayEverything.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBMPV_DLL}" "${DIST_DIR}/libmpv-2.dll"
    COMMENT "Copying PlayEverything.exe and libmpv-2.dll to portable folder ${DIST_DIR}"
)

# Shared Windows Installer/Uninstaller helpers
add_library(setup_common STATIC
    src/setup/WindowsIntegration.h
    src/setup/WindowsIntegration.cpp
)

target_include_directories(setup_common PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(setup_common PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(setup_common PUBLIC ole32 shell32 advapi32 uuid)
if(MSVC)
    target_compile_options(setup_common PRIVATE /W4 /utf-8)
endif()

# Standalone Uninstaller executable
add_executable(Uninstall WIN32
    src/setup/UninstallMain.cpp
    src/setup/Uninstall.rc
    src/ui/App.manifest
)
target_link_libraries(Uninstall PRIVATE setup_common ole32 shell32)
if(MSVC)
    target_compile_options(Uninstall PRIVATE /W4 /utf-8)
endif()

# Single-file Installer stub executable
add_executable(PlayEverythingSetupStub WIN32
    src/setup/InstallerMain.cpp
    src/setup/Installer.rc
    src/ui/App.manifest
)
target_link_libraries(PlayEverythingSetupStub PRIVATE setup_common comctl32 dwmapi ole32 shell32)
if(MSVC)
    target_compile_options(PlayEverythingSetupStub PRIVATE /W4 /utf-8)
endif()

# Pack single-file dist/PlayEverythingSetup.exe
set(INSTALLER_OUT "${CMAKE_SOURCE_DIR}/dist/PlayEverythingSetup.exe")
add_custom_target(PlayEverythingInstaller ALL
    DEPENDS PlayEverything Uninstall PlayEverythingSetupStub
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Uninstall>" "${DIST_DIR}/Uninstall.exe"
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/pack_installer.ps1"
        -StubExe "$<TARGET_FILE:PlayEverythingSetupStub>"
        -AppExe "$<TARGET_FILE:PlayEverything>"
        -MpvDll "${LIBMPV_DLL}"
        -UninstallExe "$<TARGET_FILE:Uninstall>"
        -OutputExe "${INSTALLER_OUT}"
    COMMENT "Packaging single-file installer ${INSTALLER_OUT}"
)

# Automated tests
enable_testing()

add_executable(PlayEverythingTests
    tests/main_tests.cpp
)

target_link_libraries(PlayEverythingTests PRIVATE
    playeverything_core
)

if(MSVC)
    target_compile_options(PlayEverythingTests PRIVATE /W4 /utf-8)
endif()

add_custom_command(TARGET PlayEverythingTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBMPV_DLL}" "$<TARGET_FILE_DIR:PlayEverythingTests>/libmpv-2.dll"
)

add_test(NAME PlayEverythingTests COMMAND PlayEverythingTests)
