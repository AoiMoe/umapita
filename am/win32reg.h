#pragma once

#include "am/win32util.h"

namespace AM::Win32::Reg {

namespace Bits_ {

constexpr std::size_t BUFFER_MAX = 512;

struct ErrorTag {};

struct HKeyDeleter {
  using pointer = HKEY;
  void operator () (pointer p) noexcept {
    if (p)
      RegCloseKey(p);
  }
};

using Key = std::unique_ptr<HKEY, Bits_::HKeyDeleter>;

template <typename T> struct KeyAdaptor;

template <>
class KeyAdaptor<Key> {
  const Key &m_key_;
public:
  KeyAdaptor(const Key &k) : m_key_{k} { }
  HKEY get() const noexcept { return m_key_.get(); }
};

template <>
class KeyAdaptor<HKEY> {
  HKEY m_key_;
public:
  KeyAdaptor(HKEY k) : m_key_{k} { }
  HKEY get() const noexcept { return m_key_; }
};

template <typename T>
LPBYTE to_byte_ptr(T *p) noexcept { return reinterpret_cast<LPBYTE>(p); }

template <typename T>
const BYTE *to_byte_ptr(const T *p) noexcept { return reinterpret_cast<const BYTE *>(p); }

} // namespace Bits_

using ErrorCode = ::AM::ErrorCode<LSTATUS, ERROR_SUCCESS, Bits_::ErrorTag>;
using Key = Bits_::Key;

template <typename K>
Key open_key(const K &parent, LPCTSTR name, DWORD opts, REGSAM sam) {
  HKEY hKey{};

  ErrorCode::ensure_ok(RegOpenKeyEx(Bits_::KeyAdaptor<K>(parent).get(), name, opts, sam, &hKey), "RegOpenKeyEx failed");

  return Key{hKey};
}

template <typename K>
std::pair<Key, DWORD> create_key(const K &parent, LPCTSTR name, DWORD opts, REGSAM sam, const LPSECURITY_ATTRIBUTES lpSec =nullptr) {
  HKEY hKey{};
  DWORD disp{};

  ErrorCode::ensure_ok(RegCreateKeyEx(Bits_::KeyAdaptor<K>(parent).get(), name, 0, nullptr, opts, sam, lpSec, &hKey, &disp), "RegCreateKeyEx failed");

  return std::make_pair(Key{hKey}, disp);
}

template <typename K>
tstring query_sz(const K &key, LPCTSTR name) {
  TCHAR buf[Bits_::BUFFER_MAX];
  DWORD type = REG_SZ;
  DWORD len = sizeof (buf);
  ErrorCode::ensure_ok(RegQueryValueEx(Bits_::KeyAdaptor<K>(key).get(), name, 0, &type, Bits_::to_byte_ptr(buf), &len), "RegQueryValueEx failed");
  return buf;
}

template <typename K>
DWORD query_dword(const K &key, LPCTSTR name) {
  DWORD ret;
  DWORD type = REG_DWORD;
  DWORD len = sizeof (ret);
  ErrorCode::ensure_ok(RegQueryValueEx(Bits_::KeyAdaptor<K>(key).get(), name, 0, &type, Bits_::to_byte_ptr(&ret), &len), "RegQueryValueEx failed");
  return ret;
}

template <typename K>
void set_sz(const K &key, LPCTSTR name, LPCTSTR value) {
  DWORD size = (_tcslen(value)+1) * sizeof (TCHAR);
  ErrorCode::ensure_ok(RegSetValueEx(Bits_::KeyAdaptor<K>(key).get(), name, 0, REG_SZ, Bits_::to_byte_ptr(value), size), "RegSetValueEx failed");
}

template <typename K>
void set_dword(const K &key, LPCTSTR name, DWORD value) {
  ErrorCode::ensure_ok(RegSetValueEx(Bits_::KeyAdaptor<K>(key).get(), name, 0, REG_DWORD, Bits_::to_byte_ptr(&value), sizeof (value)), "RegSetValueEx failed");
}

template <typename K>
void enum_key(const K &key, std::function<void(LPCTSTR)> f) {
  TCHAR name[Bits_::BUFFER_MAX];
  for (DWORD i = 0; RegEnumKey(Bits_::KeyAdaptor<K>(key).get(), i, name, std::size(name)) == ERROR_SUCCESS; i++) {
    f(name);
  }
}

} // namespace AM::Win32::Reg
