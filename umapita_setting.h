#pragma once

namespace UmapitaSetting {

struct PerOrientation {
  LONG monitorNumber = 0;
  bool isConsiderTaskbar;
  enum WindowArea { Whole, Client } windowArea;
  LONG size = 0;
  enum SizeAxis { Width, Height } axis = Height;
  enum Origin { N, S, W, E, NW, NE, SW, SE, C } origin = N;
  LONG offsetX = 0, offsetY = 0;
  LONG aspectX, aspectY; // XXX: アスペクト比を固定しないと計算誤差で変な比率になることがある
};

struct PerProfile {
  bool isLocked = false;
  // .xxx=xxx 記法は ISO C++20 からだが、gcc なら使えるのでヨシ！
  PerOrientation verticalSetting{
    .isConsiderTaskbar = true,
    .windowArea = PerOrientation::Whole,
    .aspectX=9,
    .aspectY=16
  };
  PerOrientation horizontalSetting{
    .isConsiderTaskbar = false,
    .windowArea = PerOrientation::Client,
    .aspectX=16,
    .aspectY=9
  };
};

constexpr PerProfile DEFAULT_PER_PROFILE{};

template <typename StringType>
struct GlobalCommonT {
  bool isEnabled = true;
  bool isCurrentProfileChanged = false;
  StringType currentProfileName{TEXT("")};  // XXX: gcc10 の libstdc++ でも basic_string は constexpr 化されてない
  template <typename T>
  GlobalCommonT<T> clone() const {
    return GlobalCommonT<T>{isEnabled, isCurrentProfileChanged, currentProfileName};
  }
};
using GlobalCommon = GlobalCommonT<AM::Win32::tstring>;

constexpr GlobalCommonT<LPCTSTR> DEFAULT_GLOBAL_COMMON{};


template <typename StringType>
struct GlobalT {
  GlobalCommonT<StringType> common{DEFAULT_GLOBAL_COMMON};
  PerProfile currentProfile{DEFAULT_PER_PROFILE};
  template <typename T>
  GlobalT<T> clone() const {
    return GlobalT<T>{common.template clone<T>(), currentProfile};
  }
};
using Global = GlobalT<AM::Win32::tstring>;

constexpr GlobalT<LPCTSTR> DEFAULT_GLOBAL{};

} // namespace UmapitaSetting
