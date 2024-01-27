#pragma once

constexpr UINT WM_TASKTRAY = WM_USER+0x1000;
constexpr UINT WM_CHANGE_PROFILE = WM_USER+0x1001;
constexpr UINT WM_KEYHOOK = WM_USER+0x1002;
constexpr UINT TASKTRAY_ID = 1;
constexpr UINT TIMER_ID = 1;
constexpr UINT TIMER_PERIOD = 200;
constexpr int HOT_KEY_ID_BASE = 1;
constexpr TCHAR TARGET_WINDOW_CLASS[] = TEXT("UnityWndClass");
constexpr TCHAR TARGET_WINDOW_NAME[] = TEXT("umamusume");
constexpr int MIN_WIDTH = 100;
constexpr int MIN_HEIGHT = 100;

// reinterpret_cast は constexpr ではないので constexpr auto REG_ROOT_KEY = HKEY_CURRENT_USER; だと通らない
#define REG_ROOT_KEY HKEY_CURRENT_USER
constexpr TCHAR REG_PROJECT_ROOT_PATH[] = TEXT("Software\\AoiMoe\\umapita");
constexpr TCHAR REG_PROFILES_SUBKEY[] = TEXT("profiles");
constexpr auto MAX_PROFILE_NAME = 100;
