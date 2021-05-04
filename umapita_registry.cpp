#include "pch.h"
#include "am/win32util.h"
#include "am/win32reg.h"
#include "am/win32reg_mapper.h"
#include "am/win32handler.h"
#include "umapita_def.h"
#include "umapita_setting.h"
#include "umapita_registry.h"

namespace Win32 = AM::Win32;
using AM::Log;

namespace UmapitaRegistry {

namespace {

using namespace AM::Win32::RegMapper;
using UmapitaSetting::PerOrientation;
using UmapitaSetting::PerProfile;
using UmapitaSetting::GlobalCommon;
using UmapitaSetting::DEFAULT_PER_PROFILE;
using UmapitaSetting::DEFAULT_GLOBAL_COMMON;

constexpr auto ENUM_WINDOW_AREA =
    make_enum_tag_map(
      make_enum_tag(TEXT("Whole"), PerOrientation::Whole),
      make_enum_tag(TEXT("Client"), PerOrientation::Client));

constexpr auto ENUM_SIZE_AXIS =
    make_enum_tag_map(
      make_enum_tag(TEXT("Width"), PerOrientation::Width),
      make_enum_tag(TEXT("Height"), PerOrientation::Height));

constexpr auto ENUM_ORIGIN =
    make_enum_tag_map(
      make_enum_tag(TEXT("N"), PerOrientation::N),
      make_enum_tag(TEXT("S"), PerOrientation::S),
      make_enum_tag(TEXT("W"), PerOrientation::W),
      make_enum_tag(TEXT("E"), PerOrientation::E),
      make_enum_tag(TEXT("NW"), PerOrientation::NW),
      make_enum_tag(TEXT("NE"), PerOrientation::NE),
      make_enum_tag(TEXT("SW"), PerOrientation::SW),
      make_enum_tag(TEXT("SE"), PerOrientation::SE));

constexpr auto PER_PROFILE_SETTING_DEF =
    make_composite_value_def<PerProfile>(
      make_bool(TEXT("isLocked"), &PerProfile::isLocked, DEFAULT_PER_PROFILE.isLocked),
      make_recurse(
        &PerProfile::vertical,
        make_s32(TEXT("vMonitorNumber"), &PerOrientation::monitorNumber, DEFAULT_PER_PROFILE.vertical.monitorNumber),
        make_bool(TEXT("vIsConsiderTaskbar"), &PerOrientation::isConsiderTaskbar, DEFAULT_PER_PROFILE.vertical.isConsiderTaskbar),
        make_enum(TEXT("vWindowArea"), &PerOrientation::windowArea, DEFAULT_PER_PROFILE.vertical.windowArea, ENUM_WINDOW_AREA),
        make_s32(TEXT("vSize"), &PerOrientation::size, DEFAULT_PER_PROFILE.vertical.size),
        make_enum(TEXT("vSizeAxis"), &PerOrientation::axis, DEFAULT_PER_PROFILE.vertical.axis, ENUM_SIZE_AXIS),
        make_enum(TEXT("vOrigin"), &PerOrientation::origin, DEFAULT_PER_PROFILE.vertical.origin, ENUM_ORIGIN),
        make_s32(TEXT("vOffsetX"), &PerOrientation::offsetX, DEFAULT_PER_PROFILE.vertical.offsetX),
        make_s32(TEXT("vOffsetY"), &PerOrientation::offsetY, DEFAULT_PER_PROFILE.vertical.offsetY),
        make_s32(TEXT("vAspectX"), &PerOrientation::aspectX, DEFAULT_PER_PROFILE.vertical.aspectX),
        make_s32(TEXT("vAspectY"), &PerOrientation::aspectY, DEFAULT_PER_PROFILE.vertical.aspectY)),
      make_recurse(
        &PerProfile::horizontal,
        make_s32(TEXT("hMonitorNumber"), &PerOrientation::monitorNumber, DEFAULT_PER_PROFILE.horizontal.monitorNumber),
        make_bool(TEXT("hIsConsiderTaskbar"), &PerOrientation::isConsiderTaskbar, DEFAULT_PER_PROFILE.horizontal.isConsiderTaskbar),
        make_enum(TEXT("hWindowArea"), &PerOrientation::windowArea, DEFAULT_PER_PROFILE.horizontal.windowArea, ENUM_WINDOW_AREA),
        make_s32(TEXT("hSize"), &PerOrientation::size, DEFAULT_PER_PROFILE.horizontal.size),
        make_enum(TEXT("hSizeAxis"), &PerOrientation::axis, DEFAULT_PER_PROFILE.horizontal.axis, ENUM_SIZE_AXIS),
        make_enum(TEXT("hOrigin"), &PerOrientation::origin, DEFAULT_PER_PROFILE.horizontal.origin, ENUM_ORIGIN),
        make_s32(TEXT("hOffsetX"), &PerOrientation::offsetX, DEFAULT_PER_PROFILE.horizontal.offsetX),
        make_s32(TEXT("hOffsetY"), &PerOrientation::offsetY, DEFAULT_PER_PROFILE.horizontal.offsetY),
        make_s32(TEXT("hAspectX"), &PerOrientation::aspectX, DEFAULT_PER_PROFILE.horizontal.aspectX),
        make_s32(TEXT("hAspectY"), &PerOrientation::aspectY, DEFAULT_PER_PROFILE.horizontal.aspectY)));

constexpr auto GLOBAL_SETTING_DEF =
    make_composite_value_def<GlobalCommon>(
      make_bool(TEXT("isEnabled"),
                           &GlobalCommon::isEnabled,
                           DEFAULT_GLOBAL_COMMON.isEnabled),
      make_bool(TEXT("isCurrentProfileChanged"),
                           &GlobalCommon::isCurrentProfileChanged,
                           DEFAULT_GLOBAL_COMMON.isCurrentProfileChanged),
      make_string(TEXT("currentProfileName"),
                             &GlobalCommon::currentProfileName,
                             DEFAULT_GLOBAL_COMMON.currentProfileName));

inline Win32::tstring encode_profile_name(Win32::StrPtr src) {
  Win32::tstring ret;

  for (; *src.ptr; src.ptr++) {
    switch (*src.ptr) {
    case TEXT(':'):
      ret += TEXT("%3A");
      break;
    case TEXT('/'):
      ret += TEXT("%2F");
      break;
    case TEXT('\\'):
      ret += TEXT("%5C");
      break;
    case TEXT('%'):
      ret += TEXT("%25");
      break;
    default:
      ret += *src.ptr;
    }
  }
  return ret;
}

Win32::tstring decode_profile_name(Win32::StrPtr src) {
  Win32::tstring ret;

  for (; *src.ptr; src.ptr++) {
    if (src.ptr[0] == TEXT('%') && _istxdigit(src.ptr[1]) && _istxdigit(src.ptr[2])) {
      TCHAR buf[3] = { src.ptr[1], src.ptr[2], L'\0' };
      TCHAR *next;
      ret += static_cast<TCHAR>(_tcstol(buf, &next, 16));
      src.ptr += 2;
    } else
      ret += *src.ptr;
  }
  return ret;
}

Win32::tstring make_regpath(Win32::StrPtr profileName) {
  Win32::tstring tmp{REG_PROJECT_ROOT_PATH};

  if (profileName.ptr) {
    tmp += TEXT("\\");
    tmp += REG_PROFILES_SUBKEY;
    if (*profileName.ptr) {
      tmp += TEXT("\\");
      tmp += encode_profile_name(profileName);
    }
  }
  return tmp;
}

} // namespace UmapitaRegistry::Bits_

UmapitaSetting::PerProfile load_setting(Win32::StrPtr profileName) {
  auto path = make_regpath(profileName);

  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path, 0, KEY_READ);
    try {
      return PER_PROFILE_SETTING_DEF.get(key);
    }
    catch (Win32::RegMapper::GetFailed &) {
      return UmapitaSetting::DEFAULT_PER_PROFILE;
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%ls\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
    return UmapitaSetting::DEFAULT_PER_PROFILE;
  }
}

void save_setting(Win32::StrPtr profileName, const UmapitaSetting::PerProfile &s) {
  auto path = make_regpath(profileName);

  try {
    [[maybe_unused]] auto [key, disp] = Win32::Reg::create_key(REG_ROOT_KEY, path, 0, KEY_WRITE);
    try {
      PER_PROFILE_SETTING_DEF.put(key, s);
    }
    catch (Win32::RegMapper::PutFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%ls\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
}

UmapitaSetting::Global load_global_setting() {
  auto load_common =
      []() {
        auto path = make_regpath(nullptr);
        try {
          auto key = Win32::Reg::open_key(REG_ROOT_KEY, path, 0, KEY_READ);
          try {
            return GLOBAL_SETTING_DEF.get(key);
          }
          catch (Win32::RegMapper::GetFailed &) {
            return UmapitaSetting::DEFAULT_GLOBAL_COMMON.clone<Win32::tstring>();
          }
        }
        catch (Win32::Reg::ErrorCode &ex) {
          Log::debug(TEXT("cannot read registry \"%ls\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
          return UmapitaSetting::DEFAULT_GLOBAL_COMMON.clone<Win32::tstring>();
        }
      };
  return UmapitaSetting::Global{load_common(), load_setting(nullptr)};
}

void save_global_setting(const UmapitaSetting::Global &s) {
  auto path = make_regpath(nullptr);

  try {
    [[maybe_unused]] auto [key, disp ] = Win32::Reg::create_key(REG_ROOT_KEY, path, 0, KEY_WRITE);
    try {
      GLOBAL_SETTING_DEF.put(key, s.common);
    }
    catch (Win32::RegMapper::PutFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%ls\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
  save_setting(nullptr, s.currentProfile);
}

std::vector<Win32::tstring> enum_profile() {
  std::vector<Win32::tstring> ret;

  Win32::tstring path{REG_PROJECT_ROOT_PATH};
  path += TEXT("\\");
  path += REG_PROFILES_SUBKEY;
  try {
    [[maybe_unused]] auto [key, disp] = Win32::Reg::create_key(REG_ROOT_KEY, path, 0, KEY_READ);
    Win32::Reg::enum_key(key, [&ret](Win32::tstring name) { ret.emplace_back(decode_profile_name(name)); });
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot enum registry \"%ls\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }

  return ret;
}

void delete_profile(Win32::StrPtr name) {
  auto path = make_regpath(TEXT(""));
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path, 0, KEY_WRITE);
    Win32::Reg::delete_tree(key, encode_profile_name(name));
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot delete \"%ls\": %hs(reason=%d, path=\"%ls\")"), name, ex.what(), ex.code, path.c_str());
  }
}

Win32::tstring rename_profile(Win32::StrPtr oldName, Win32::StrPtr newName) {
  UmapitaRegistry::delete_profile(newName);
  auto path = make_regpath(TEXT(""));
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path, 0, KEY_READ);
    Win32::Reg::rename_key(key, encode_profile_name(oldName), encode_profile_name(newName));
    return newName.ptr;
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot rename \"%ls\" to \"%ls\": %hs(reason=%d)"), oldName, newName, ex.what(), ex.code);
    return oldName.ptr;
  }
}

bool is_profile_existing(Win32::StrPtr name) {
  auto ps = enum_profile();
  return std::find(ps.begin(), ps.end(), name.ptr) != ps.end();
}

} // namespace UmapitaRegistry
