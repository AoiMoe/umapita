#pragma once

namespace AM::Win32 {

template <class T>
T make_sized_pod() {
  T pod;
  memset(&pod, 0, sizeof (pod));
  pod.cbSize = sizeof (pod);
  return pod;
}

inline auto width(const RECT &r) -> auto { return r.right - r.left; }
inline auto height(const RECT &r) -> auto { return r.bottom - r.top; }
inline auto extent(const RECT &r) -> auto { return std::make_pair(width(r), height(r)); }

namespace Op {

inline bool operator == (const RECT &lhs, const RECT &rhs) {
  return lhs.left == rhs.left && lhs.right == rhs.right && lhs.top == rhs.top && lhs.bottom == rhs.bottom;
}

template <typename T>
bool operator != (const T &lhs, const T &rhs) { return !(lhs == rhs); }

} // namespace Op


} // namespace AM::Win32
