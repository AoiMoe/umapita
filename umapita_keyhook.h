#pragma once

using EnableKeyhookFun = bool (*)(HWND hWndReceiver, UINT wm, DWORD targetTid);
using DisableKeyhookFun = bool (*)();
using IsKeyhookEnabledFun = bool (*)();
struct KeyhookEntry {
  EnableKeyhookFun enable_keyhook;
  DisableKeyhookFun disable_keyhook;
  IsKeyhookEnabledFun is_keyhook_enabled;
};
using GetEntryFun = const KeyhookEntry *(*)();
constexpr char KEYHOOK_GET_ENTRY_FUN_NAME[] = "get_entry";
constexpr TCHAR KEYHOOK_DLL_NAME[] = TEXT("umapita_keyhook.dll");
