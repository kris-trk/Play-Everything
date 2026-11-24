#pragma once

// App Icon Resource ID (lowest ID = default Explorer .exe icon)
#define IDI_APP_ICON            101

// Top Menu Command IDs
#define IDM_FILE_OPEN           1001
#define IDM_FILE_CLOSE          1002
#define IDM_FILE_EXIT           1003

#define IDM_PLAY_TOGGLE         2001
#define IDM_PLAY_RESTART        2002
#define IDM_PLAY_BACK15         2003
#define IDM_PLAY_FWD15          2004
#define IDM_PLAY_LOOP           2005
#define IDM_PLAY_FULLSCREEN     2006

#define IDM_SPEED_025           2101
#define IDM_SPEED_050           2102
#define IDM_SPEED_075           2103
#define IDM_SPEED_100           2104
#define IDM_SPEED_200           2105
#define IDM_SPEED_300           2106
#define IDM_SPEED_CUSTOM        2107

#define IDM_PLAYLIST_TOGGLE     2201
#define IDM_PLAYLIST_ADD        2202
#define IDM_PLAYLIST_PREV       2203
#define IDM_PLAYLIST_NEXT       2204
#define IDM_PLAYLIST_CLEAR      2205
#define IDM_PLAYLIST_SHUFFLE    2206

#define IDM_INFO_SHOW           2301

#define IDM_SETTINGS_KEYBINDS   3001
#define IDM_SETTINGS_UNINSTALL  3002

// Custom window messages
#define WM_APP_MPV_WAKEUP       (WM_APP + 1)
#define WM_APP_OPEN_FILE_REQ    (WM_APP + 2)
#define WM_APP_PREVIEW_READY    (WM_APP + 3)
