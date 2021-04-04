#pragma once

#include "am/win32util.h"

namespace AM::Win32::Reg {

namespace Bits_ {

struct ErrorTag {};
using ErrorCode = ::AM::ErrorCode<LSTATUS, ERROR_SUCCESS, ErrorTag>;

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

struct TypeUnmatched : RuntimeError<TypeUnmatched> {
  DWORD type = 0;
  TypeUnmatched() : RuntimeError<TypeUnmatched>{} { }
  TypeUnmatched(DWORD t, const char *msg) : RuntimeError<TypeUnmatched>{msg}, type{t} { }
};

struct SizeUnmatched : LogicError<SizeUnmatched> {
  SizeUnmatched() : LogicError<SizeUnmatched>{} { }
  SizeUnmatched(std::size_t, const char *msg) : LogicError<SizeUnmatched>{msg} { }
};

struct TypeSize {
  DWORD type;
  DWORD size;
};

template <typename K>
TypeSize query(const K &key, LPCTSTR name, void *buf, DWORD size) {
  auto hkey = Bits_::KeyAdaptor<K>(key).get();
  DWORD type;
  ErrorCode::ensure_ok(RegQueryValueEx(hkey, name, 0, &type, to_byte_ptr(buf), &size), "RegQueryValueEx failed");
  return TypeSize{type, size};
}

template <typename K>
TypeSize query_type_size(const K &key, LPCTSTR name) {
  return query(key, name, nullptr, 0);
}

template <DWORD TypeIndex>
TypeSize ensure_type(TypeSize typesize) {
  ensure_expected<TypeUnmatched, TypeIndex>(typesize.type);
  return typesize;
}

template <DWORD TypeIndex, typename T>
TypeSize ensure_type_size(TypeSize typesize) {
  ensure_expected<SizeUnmatched, sizeof (T)>(typesize.size);
  return ensure_type<TypeIndex>(typesize);
}

} // namespace Bits_

using ErrorCode = Bits_::ErrorCode;
using Key = Bits_::Key;
using TypeUnmatched = Bits_::TypeUnmatched;
using SizeUnmatched = Bits_::SizeUnmatched;


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
  auto [type, size] = Bits_::ensure_type<REG_SZ>(Bits_::query_type_size(key, name));
  return Win32::get_sz(size/sizeof (TCHAR) - 1, [&](LPTSTR buf) { Bits_::ensure_type<REG_SZ>(Bits_::query(key, name, buf, size)); });
}

template <typename K>
DWORD query_dword(const K &key, LPCTSTR name) {
  DWORD ret;
  Bits_::ensure_type_size<REG_DWORD, DWORD>(Bits_::query(key, name, &ret, sizeof (ret)));
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

template <typename K, typename F>
void enum_key(const K &key, F f) {
  HKEY hKey = Bits_::KeyAdaptor<K>(key).get();
  DWORD numSubKeys;
  DWORD maxSubKeyLen;
  ErrorCode::ensure_ok(RegQueryInfoKey(hKey,
                                       nullptr, nullptr,
                                       nullptr,
                                       &numSubKeys, &maxSubKeyLen, nullptr,
                                       nullptr, nullptr, nullptr,
                                       nullptr,
                                       nullptr));
  for (DWORD i = 0; i < numSubKeys; i++) {
    auto name = Win32::get_sz(maxSubKeyLen,
                              [=](LPTSTR buf) {
                                ErrorCode::ensure_ok(RegEnumKey(hKey, i, buf, maxSubKeyLen+1));
                                return _tcslen(buf);
                              });
    f(name);
  }
}

} // namespace AM::Win32::Reg
