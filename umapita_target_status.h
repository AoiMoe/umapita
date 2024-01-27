#pragma once

namespace Umapita {

//
// 監視対象ウィンドウの状態
//
struct TargetStatus {
  AM::Win32::Window window;
  bool isFocusOn;
  RECT windowRect{0, 0, 0, 0};
  RECT clientRect{0, 0, 0, 0};
  static TargetStatus get(AM::Win32::StrPtr winclass, AM::Win32::StrPtr winname);
  void adjust(const UmapitaMonitors &monitors, const UmapitaSetting::PerProfile &profile);
};

inline bool operator == (const TargetStatus &lhs, const TargetStatus &rhs) {
  using AM::Win32::Op::operator ==;
  return lhs.window == rhs.window && (!lhs.window || (lhs.isFocusOn == rhs.isFocusOn &&
                                                      lhs.windowRect == rhs.windowRect &&
                                                      lhs.clientRect == rhs.clientRect));
}

inline bool operator != (const TargetStatus &lhs, const TargetStatus &rhs) {
  return !(lhs == rhs);
}

} // namespace Umapita
