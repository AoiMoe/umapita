#pragma once

namespace Bits_ {
using EnableKeyhookFun = bool (*)(HWND hWndReceiver, UINT wm, DWORD targetTid);
using DisableKeyhookFun = bool (*)();
using IsKeyhookEnabledFun = bool (*)();
struct KeyhookEntry {
  EnableKeyhookFun enable;
  DisableKeyhookFun disable;
  IsKeyhookEnabledFun is_enabled;
};
using GetEntryFun = const KeyhookEntry *(*)();
constexpr char KEYHOOK_GET_ENTRY_FUN_NAME[] = "get_entry";
constexpr TCHAR KEYHOOK_DLL_NAME[] = TEXT("umapita_keyhook.dll");
} // namespace Bits_

#ifndef UMAPITA_KEYHOOK_DLL
class KeyHook {
  HMODULE m_hModKeyHook = nullptr;
  const Bits_::KeyhookEntry *m_keyhookEntry = nullptr;
  //
  static KeyHook s_keyhook;
  //
  KeyHook() noexcept { }
  KeyHook(const KeyHook &) = delete;
  KeyHook &operator = (const KeyHook &) = delete;
  void load_() noexcept {
    if (!m_hModKeyHook) {
      m_hModKeyHook = LoadLibrary(Bits_::KEYHOOK_DLL_NAME);
      AM::Log::debug(TEXT("m_hModKeyHook=%p"), m_hModKeyHook);
      if (m_hModKeyHook) {
        auto f = reinterpret_cast<Bits_::GetEntryFun>(reinterpret_cast<void *>(GetProcAddress(m_hModKeyHook, Bits_::KEYHOOK_GET_ENTRY_FUN_NAME)));
        AM::Log::debug(TEXT("get_entry=%p"), f);
        if (f) {
          m_keyhookEntry = f();
          AM::Log::debug(TEXT("s_keyhookEntry=%p"), m_keyhookEntry);
        }
      }
      if (!m_keyhookEntry) {
        AM::Log::info(TEXT("cannot load \"%S\""), Bits_::KEYHOOK_DLL_NAME);
      }
    }
  }
  void unload_() noexcept {
    m_keyhookEntry = nullptr;
    if (m_hModKeyHook) {
      FreeLibrary(m_hModKeyHook);
      AM::Log::info(TEXT("unload \"%S\""), Bits_::KEYHOOK_DLL_NAME);
    }
    m_hModKeyHook = nullptr;
  }
  bool is_available_() const noexcept { return !!m_keyhookEntry; }
  bool is_enabled_() const noexcept { return is_available_() && m_keyhookEntry->is_enabled(); }
  bool enable_(HWND hWnd, UINT wm, DWORD targetTid) const noexcept {
    return is_available_() && m_keyhookEntry->enable(hWnd, wm, targetTid);
  }
  bool disable_() const noexcept {
    return is_available_() && m_keyhookEntry->disable();
  }
public:
  ~KeyHook() noexcept { unload(); }
  static void load() { s_keyhook.load_(); }
  static void unload() { s_keyhook.unload_(); }
  static bool is_available() noexcept { return s_keyhook.is_available_(); }
  static bool is_enabled() noexcept { return s_keyhook.is_enabled_(); }
  static bool enable(HWND hWnd, UINT wm, DWORD targetTid) noexcept { return s_keyhook.enable_(hWnd, wm, targetTid); }
  static bool disable() noexcept { return s_keyhook.disable_(); }
};
KeyHook KeyHook::s_keyhook;
#endif // !UMAPITA_KEYHOOK_DLL
