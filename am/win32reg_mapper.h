#pragma once

#include "am/win32reg.h"

namespace AM::Win32::RegMapper {

struct GetFailed : AM::RuntimeError<GetFailed> { };
struct PutFailed : AM::RuntimeError<PutFailed> { };

// フィールド定義と構造体上のエントリを結びつけるアダプタ
template <class FieldDefT, class Struct, typename Type>
struct FieldBinder {
  FieldDefT fieldDef;
  Type Struct::*fieldOffset;
};

// あるキーに属する一つの値を、一つの変数に対応させるフィールド定義
template <class TypeMap>
struct SingleValueDef {
  using ValueType = typename TypeMap::ValueType;
  using DefaultValueType = typename TypeMap::DefaultValueType;
  LPCTSTR name;
  TypeMap typeMap;
  DefaultValueType defaultValue;
  auto get(const Win32::Reg::Key &key) const {
    try {
      return typeMap.get(key, name);
    }
    catch (Win32::Reg::ErrorCode &ex) {
      if (ex.code == ERROR_FILE_NOT_FOUND) {
        Log::warning(TEXT("SingleValueDef::get: \"%ls\" is not found - use default value"), name);
        return ValueType{defaultValue};
      }
      Log::error(TEXT("SingleValueDef::get: cannot get \"%ls\" value: %hs(%d)"), name, ex.what(), ex.code);
      throw GetFailed{};
    }
  }
  auto put(const Win32::Reg::Key &key, const ValueType &v) const {
    try {
      return typeMap.put(key, name, v);
    }
    catch (Win32::Reg::ErrorCode &ex) {
      Log::error(TEXT("SingleValueDef::put: cannot put \"%ls\" value: %hs(%d)"), name, ex.what(), ex.code);
      throw PutFailed{};
    }
  }
};

template <class TypeMap>
constexpr auto make_single_value_def(LPCTSTR name, TypeMap map, typename TypeMap::DefaultValueType defval) {
  return SingleValueDef<TypeMap>{name, map, defval};
}

template <class TypeMap, class Struct, typename Type>
constexpr auto make_binder(SingleValueDef<TypeMap> def, Type Struct::*ofs) {
  return FieldBinder<decltype (def), Struct, Type>{def, ofs};
}

// ある一つのキーの上の複数の値を、一つの構造体変数に対応させるフィールド定義
template <class Struct, class ...FieldBinderTypes>
struct CompositeValueDef {
  using ValueType = Struct;
  std::tuple<FieldBinderTypes...> fieldDefs;
  Struct get(const Win32::Reg::Key &key) const {
    Struct values;
    tuple_foreach(fieldDefs, [&key, &values](auto /* FieldBinder<...> */ const &d) {
                               values.*(d.fieldOffset) = d.fieldDef.get(key);
                             });
    return values;
  }
  void put(const Win32::Reg::Key &key, const Struct &values) const {
    tuple_foreach(fieldDefs, [&key, &values](auto /* FieldBinder<...> */ const &d) {
                               d.fieldDef.put(key, values.*(d.fieldOffset));
                             });
  }
};

template <class Struct, class ...FieldBinderTypes>
constexpr auto make_composite_value_def(FieldBinderTypes ...binders) {
  return CompositeValueDef<Struct, FieldBinderTypes...>{std::make_tuple(binders...)};
}

template <class Struct, typename Type, typename ...FieldBinderTypes>
constexpr auto make_binder(CompositeValueDef<Type, FieldBinderTypes...> def, Type Struct::*ofs) {
  return FieldBinder<decltype (def), Struct, Type>{def, ofs};
}

template <class Struct, class Type, class ...FieldBinderTypes>
constexpr auto make_recurse(Type Struct::*ofs, FieldBinderTypes ...binders) {
  return make_binder(make_composite_value_def<Type>(binders...), ofs);
}

//
// 列挙体型マッパ
//
template <typename Enum>
using EnumTag = std::pair<LPCTSTR, Enum>;

template <typename Enum>
constexpr auto make_enum_tag(LPCTSTR tag, Enum val) {
  return EnumTag<Enum>{tag, val};
}

template <typename Enum, std::size_t Num>
using EnumTagMap = std::array<EnumTag<Enum>, Num>;

template <typename Enum, typename ...Args>
constexpr auto make_enum_tag_map(EnumTag<Enum> arg0, Args ...args) {
  return EnumTagMap<Enum, sizeof...(Args)+1>{{arg0, args...}};
}

template <typename Enum, std::size_t Num>
struct EnumMap {
  using ValueType = Enum;
  using DefaultValueType = Enum;
  EnumTagMap<Enum, Num> tagMap;
  Enum get(const Win32::Reg::Key &key, LPCTSTR fname) const {
    auto str = Win32::Reg::query_sz(key, fname);
    Log::debug(TEXT("EnumMap::get: %ls -> %ls"), fname, str.c_str());
    for (auto [t, v] : tagMap) {
      if (str == t)
        return v;
    }
    Log::error(TEXT("unknown enum tag in \"%ls\": %ls"), fname, str.c_str());
    throw GetFailed{};
  }
  void put(const Win32::Reg::Key &key, LPCTSTR fname, Enum v) const {
    LPCTSTR tag = [this, v]() {
                    for (auto [t, x] : tagMap) {
                      if (v == x)
                        return t;
                    }
                    throw PutFailed{};
                  }();
    Log::debug(TEXT("EnumMap::put: %ls -> %ls"), fname, tag);
    Win32::Reg::set_sz(key, fname, tag);
  }
};

template <typename Enum, std::size_t Num>
constexpr auto make_enum(EnumTagMap<Enum, Num> tags) {
  return EnumMap<Enum, Num>{tags};
}

template <typename Enum, std::size_t Num>
constexpr auto make_enum(LPCTSTR name, Enum defval, EnumTagMap<Enum, Num> tags) {
  return make_single_value_def(name, make_enum(tags), defval);
}

template <typename Enum, class Struct, std::size_t Num>
constexpr auto make_enum(LPCTSTR name, Enum Struct::*ofs, Enum defval,  EnumTagMap<Enum, Num> tags) {
  return make_binder(make_enum(name, defval, tags), ofs);
}

//
// bool 型マッパ
//
struct BoolMap {
  using ValueType = bool;
  using DefaultValueType = bool;
  bool get(const Win32::Reg::Key &key, LPCTSTR fname) const {
    bool v = !!Win32::Reg::query_dword(key, fname);
    Log::debug(TEXT("BoolMap::get: %ls -> %hs"), fname, v ? "true":"false");
    return v;
  }
  void put(const Win32::Reg::Key &key, LPCTSTR fname, bool v) const {
    Log::debug(TEXT("BoolMap::put: %ls -> %hs"), fname, v ? "true":"false");
    Win32::Reg::set_dword(key, fname, static_cast<DWORD>(v ? 1 : 0));
  }
};

inline constexpr auto make_bool() {
  return BoolMap{};
}

inline constexpr auto make_bool(LPCTSTR name, bool defval) {
  return make_single_value_def(name, make_bool(), defval);
}

template <class Struct>
constexpr auto make_bool(LPCTSTR name, bool Struct::*ofs, bool defval) {
  return make_binder(make_bool(name, defval), ofs);
}

//
// 32bit 符号付き整数型マッパ
//
struct S32Map {
  using ValueType = INT32;
  using DefaultValueType = INT32;
  INT32 get(const Win32::Reg::Key &key, LPCTSTR fname) const {
    INT32 v = static_cast<INT32>(Win32::Reg::query_dword(key, fname));
    Log::debug(TEXT("LongMap::get: %ls -> %d"), fname, static_cast<int>(v));
    return v;
  }
  void put(const Win32::Reg::Key &key, LPCTSTR fname, INT32 v) const {
    Log::debug(TEXT("LongMap::put: %ls -> %d"), fname, static_cast<int>(v));
    Win32::Reg::set_dword(key, fname, static_cast<DWORD>(v));
  }
};

inline constexpr auto make_s32() {
  return S32Map{};
}

inline constexpr auto make_s32(LPCTSTR name, INT32 defval) {
  return make_single_value_def(name, make_s32(), defval);
}

template <typename T, typename U, class Struct>
constexpr auto make_s32(LPCTSTR name, T Struct::*ofs, U defval) {
  return make_binder(make_s32(name, defval), ofs);
}

//
// 文字列型マッパ
//
struct StringMap {
  using ValueType = Win32::tstring;
  using DefaultValueType = LPCTSTR;
  Win32::tstring get(const Win32::Reg::Key &key, LPCTSTR fname) const {
    auto str = Win32::Reg::query_sz(key, fname);
    Log::debug(TEXT("StringMap::get: %ls -> %ls"), fname, str.c_str());
    return str;
  }
  void put(const Win32::Reg::Key &key, LPCTSTR fname, const Win32::tstring &v) const {
    Log::debug(TEXT("StringMap::put: %ls -> %ls"), fname, v.c_str());
    Win32::Reg::set_sz(key, fname, v.c_str());
  }
};

inline constexpr auto make_string() {
  return StringMap{};
}

inline constexpr auto make_string(LPCTSTR name, LPCTSTR defval) {
  return make_single_value_def(name, make_string(), defval);
}

template <class Struct>
constexpr auto make_string(LPCTSTR name, tstring Struct::*ofs, LPCTSTR defval) {
  return make_binder(make_string(name, defval), ofs);
}

} // namespace AM::Win32::RegMapper
