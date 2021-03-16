#pragma once

#include "am/util.h"

namespace AM::Win32 {

using tstring = std::basic_string<TCHAR>;

//
// make POD struct having cbSize field
//
template <class T>
T make_sized_pod() {
  T pod;
  memset(&pod, 0, sizeof (pod));
  pod.cbSize = sizeof (pod);
  return pod;
}

//
// for RECT struct
//
inline auto width(const RECT &r) -> auto { return r.right - r.left; }
inline auto height(const RECT &r) -> auto { return r.bottom - r.top; }
inline auto extent(const RECT &r) -> auto { return std::make_pair(width(r), height(r)); }

//
// convenience operators
//
namespace Op {

inline bool operator == (const RECT &lhs, const RECT &rhs) {
  return lhs.left == rhs.left && lhs.right == rhs.right && lhs.top == rhs.top && lhs.bottom == rhs.bottom;
}

template <typename T>
bool operator != (const T &lhs, const T &rhs) { return !(lhs == rhs); }

} // namespace Op

//
// system errors
//
struct SystemErrorTag {};
using SystemErrorCode = ErrorCode<DWORD, ERROR_SUCCESS, SystemErrorTag>;

} // namespace AM::Win32
