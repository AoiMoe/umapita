#include "pch.h"
#include "umapita_keyhook.h"

#define DPR(a)

// XXX: for MSC, add some #pragma
#define SHARED __attribute__((section(".shared"), shared))

static DWORD g_ownerPid SHARED = 0;
static HWND g_hWndReceiver SHARED = nullptr;
static UINT g_wm SHARED = 0;
static HHOOK g_hHook SHARED = nullptr;

static HINSTANCE g_hInstance = nullptr;

static CALLBACK LRESULT KeyboardHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    if (g_hHook && g_hWndReceiver)
      PostMessage(g_hWndReceiver, g_wm, wParam, lParam);
  }
  return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

static bool enable_keyhook(HWND hWndReceiver, UINT wm, DWORD targetTid) {
  // 呼び出し側で排他が必要
  if (g_hHook)
    return false;
  g_hHook = SetWindowsHookEx(WH_KEYBOARD, KeyboardHook, g_hInstance, targetTid);
  if (g_hHook) {
    g_hWndReceiver = hWndReceiver;
    g_ownerPid = GetCurrentProcessId();
    g_wm = wm;
  }
  return true;
}

static bool is_keyhook_enabled() {
  return g_hHook && g_ownerPid == GetCurrentProcessId();
}

static bool disable_keyhook() {
  // 呼び出し側で排他が必要
  if (is_keyhook_enabled()) {
    UnhookWindowsHookEx(g_hHook);
    g_hHook = nullptr;
    g_ownerPid = 0;
    g_hWndReceiver = nullptr;
    g_wm = 0;
    return true;
  }
  return false;
}

static const KeyhookEntry keyhook_entry = {&enable_keyhook, &disable_keyhook, &is_keyhook_enabled};
extern "C" {
  __declspec(dllexport) const KeyhookEntry *get_entry() {
    return &keyhook_entry;
  }
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID) {
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    DPR(("attach=%lX\n", GetCurrentProcessId()));
    g_hInstance = reinterpret_cast<HINSTANCE>(hModule);
    break;
  case DLL_PROCESS_DETACH:
    DPR(("detach=%lX\n", GetCurrentProcessId()));
    disable_keyhook();
    break;
  }
  return TRUE;
}
