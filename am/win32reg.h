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

struct KeyHandle {
  HKEY hKey;
  KeyHandle(HKEY k) : hKey{k} { }
  KeyHandle(const Key &k) : hKey{k.get()} { }
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

inline TypeSize query(KeyHandle key, StrPtr name, void *buf, DWORD size) {
  DWORD type;
  ErrorCode::ensure_ok(RegQueryValueEx(key.hKey, name.ptr, 0, &type, to_byte_ptr(buf), &size), "RegQueryValueEx failed");
  return TypeSize{type, size};
}

inline TypeSize query_type_size(KeyHandle key, StrPtr name) {
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


inline Key open_key(Bits_::KeyHandle parent, StrPtr name, DWORD opts, REGSAM sam) {
  HKEY hKey{};

  ErrorCode::ensure_ok(RegOpenKeyEx(parent.hKey, name.ptr, opts, sam, &hKey), "RegOpenKeyEx failed");

  return Key{hKey};
}

inline std::pair<Key, DWORD> create_key(Bits_::KeyHandle parent, StrPtr name, DWORD opts, REGSAM sam, const LPSECURITY_ATTRIBUTES lpSec =nullptr) {
  HKEY hKey{};
  DWORD disp{};

  ErrorCode::ensure_ok(RegCreateKeyEx(parent.hKey, name.ptr, 0, nullptr, opts, sam, lpSec, &hKey, &disp), "RegCreateKeyEx failed");

  return std::make_pair(Key{hKey}, disp);
}

inline tstring query_sz(Bits_::KeyHandle key, StrPtr name) {
  auto [type, size] = Bits_::ensure_type<REG_SZ>(Bits_::query_type_size(key, name));
  return Win32::get_sz(size/sizeof (TCHAR) - 1, [&](LPTSTR buf) { Bits_::ensure_type<REG_SZ>(Bits_::query(key, name, buf, size)); });
}

inline DWORD query_dword(Bits_::KeyHandle key, StrPtr name) {
  DWORD ret;
  Bits_::ensure_type_size<REG_DWORD, DWORD>(Bits_::query(key, name, &ret, sizeof (ret)));
  return ret;
}

inline void set_sz(Bits_::KeyHandle key, StrPtr name, StrPtr value) {
  DWORD size = (_tcslen(value.ptr)+1) * sizeof (TCHAR);
  ErrorCode::ensure_ok(RegSetValueEx(key.hKey, name.ptr, 0, REG_SZ, Bits_::to_byte_ptr(value.ptr), size), "RegSetValueEx failed");
}

inline void set_dword(Bits_::KeyHandle key, StrPtr name, DWORD value) {
  ErrorCode::ensure_ok(RegSetValueEx(key.hKey, name.ptr, 0, REG_DWORD, Bits_::to_byte_ptr(&value), sizeof (value)), "RegSetValueEx failed");
}

template <typename F>
void enum_key(Bits_::KeyHandle key, F f) {
  DWORD numSubKeys;
  DWORD maxSubKeyLen;
  ErrorCode::ensure_ok(RegQueryInfoKey(key.hKey,
                                       nullptr, nullptr,
                                       nullptr,
                                       &numSubKeys, &maxSubKeyLen, nullptr,
                                       nullptr, nullptr, nullptr,
                                       nullptr,
                                       nullptr));
  for (DWORD i = 0; i < numSubKeys; i++) {
    auto name = Win32::get_sz(maxSubKeyLen,
                              [=](LPTSTR buf) {
                                ErrorCode::ensure_ok(RegEnumKey(key.hKey, i, buf, maxSubKeyLen+1));
                                return _tcslen(buf);
                              });
    f(name);
  }
}

inline void rename_key(Bits_::KeyHandle key, StrPtr oldName, StrPtr newName) {
  ErrorCode::ensure_ok(RegRenameKey(key.hKey, oldName.ptr, newName.ptr), "RegRenameKey failed");
}

inline void delete_key(Bits_::KeyHandle key, StrPtr name) {
  ErrorCode::ensure_ok(RegDeleteKey(key.hKey, name.ptr), "RegDeleteKey failed");
}

inline void delete_tree(Bits_::KeyHandle key, StrPtr name) {
  ErrorCode::ensure_ok(RegDeleteTree(key.hKey, name.ptr), "RegDeleteTree failed");
}

} // namespace AM::Win32::Reg
