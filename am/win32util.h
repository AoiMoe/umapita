#pragma once

#include "am/util.h"

namespace AM::Win32 {

using tstring = std::basic_string<TCHAR>;

inline tstring remove_ws_on_both_ends(const tstring &src) {
  auto b = std::find_if(src.begin(), src.end(), [](WCHAR c) { return !_istspace(c); });
  auto e = std::find_if(src.rbegin(), src.rend(), [](WCHAR c) { return !_istspace(c); }).base();
  return b < e ? tstring{b, e} : tstring{};
}

//
// get null-terminated string from Win32 API and return it as tstring
//
template <class GetFunc>
tstring get_sz(std::size_t len, GetFunc gf) {
  tstring ret(len+1, TCHAR{});
  if constexpr (std::is_invocable_v<decltype(gf), LPTSTR, std::size_t>) {
    if constexpr (std::is_void_v<std::invoke_result_t<decltype(gf), LPTSTR, std::size_t>>) {
      gf(ret.data(), len);
      ret.resize(len);
    } else {
      auto actuallen = gf(ret.data(), len);
      ret.resize(actuallen);
    }
  } else {
    if constexpr (std::is_void_v<std::invoke_result_t<decltype(gf), LPTSTR>>) {
      gf(ret.data());
      ret.resize(len);
    } else {
      auto actuallen = gf(ret.data());
      ret.resize(actuallen);
    }
  }
  return ret;
}

template <class GetFunc>
tstring get_sz(GetFunc gf) {
  return get_sz(gf(nullptr, 0), gf);
}

inline tstring get_window_text(HWND hWnd) {
  return Win32::get_sz(GetWindowTextLength(hWnd),
                       [=](LPTSTR buf, std::size_t len) { GetWindowText(hWnd, buf, len+1); });
}

inline tstring load_string(HINSTANCE hInstance, UINT id) {
  return Win32::get_sz([=]() {
                         LPVOID *ptr;
                         return LoadString(hInstance, id, reinterpret_cast<LPTSTR>(&ptr), 0);
                       }(),
                       [=](LPTSTR buf, std::size_t len) { LoadString(hInstance, id, buf, len+1); });
}


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


//
// win32 errors
//
struct Win32ErrorCode : SystemErrorCode {
  template <typename Ret>
  explicit Win32ErrorCode(Ret, const char *msg = nullptr) : SystemErrorCode{GetLastError(), msg} { }
};


//
// icon
//
namespace Bits_ {

struct HIconDeleter {
  using pointer = HICON;
  void operator () (pointer p) noexcept {
    if (p)
      DestroyIcon(p);
  }
};

using Icon = std::unique_ptr<HICON, HIconDeleter>;

} // namespace Bits_

using Icon = Bits_::Icon;
inline Icon load_icon_image(HINSTANCE hInst, LPCTSTR name, int cx, int cy, UINT fuLoad) {
  if ((fuLoad & LR_SHARED))
    throw IllegalArgument("load_icon_image cannot accept LR_SHARED");
  return Icon{reinterpret_cast<HICON>(LoadImage(hInst, name, IMAGE_ICON, cx, cy, fuLoad))};
}

//
// menu
//
namespace Bits_ {

struct HMenuDeleter {
  using pointer = HMENU;
  void operator () (pointer p) noexcept {
    if (p)
      DestroyMenu(p);
  }
};

struct HBorrowedMenuDeleter {
  using pointer = HMENU;
  void operator () (pointer) noexcept {
  }
};

using Menu = std::unique_ptr<HMENU, HMenuDeleter>;
using BorrowedMenu = std::unique_ptr<HMENU, HBorrowedMenuDeleter>;

} // namespace Bits_

using Menu = Bits_::Menu;
using BorrowedMenu = Bits_::BorrowedMenu;

inline Menu create_popup_menu() {
  return Menu{CreatePopupMenu()};
}

inline Menu load_menu(HINSTANCE hInstance, LPCTSTR lpMenu) {
  return Menu{LoadMenu(hInstance, lpMenu)};
}

template <typename T>
BorrowedMenu get_sub_menu(const T &m, int n) {
  return BorrowedMenu{throw_if<Win32ErrorCode, HMENU, nullptr>(GetSubMenu(m.get(), n))};
}

//
// create GDI objects
//

namespace Bits_ {

template <typename Handle>
struct HGdiObjDeleter {
  using pointer = Handle;
  void operator () (pointer p) noexcept {
    if (p)
      DeleteObject(reinterpret_cast<HGDIOBJ>(p));
  }
};

using Region = std::unique_ptr<HRGN, HGdiObjDeleter<HRGN>>;

} // namespace Bits_

using Region = Bits_::Region;
inline Region create_rect_region(int x1, int y1, int x2, int y2) {
  return Region{CreateRectRgn(x1, y1, x2, y2)};
}


//
// scoped HDC modifiers
//
namespace Bits_ {

template <class Traits>
class ScopedHDCRestorer {
  using V = typename Traits::ValueType;
  HDC m_hdc;
  V m_val;
public:
  void reset(std::pair<HDC, V> pair = std::pair<HDC, V>(nullptr, Traits::INVALID)) noexcept {
    if (m_hdc && m_val != Traits::INVALID)
      Traits::restore(m_hdc, m_val);
    m_hdc = nullptr;
    m_val = Traits::INVALID;
  }
  std::pair<HDC, V> release() noexcept {
    auto ret = std::make_pair(m_hdc, m_val);
    m_hdc = nullptr;
    m_val = Traits::INVALID;
    return ret;
  }
  ~ScopedHDCRestorer() noexcept { reset(); }
  ScopedHDCRestorer(HDC hdc, V val) noexcept : m_hdc{hdc}, m_val{val} { }
  ScopedHDCRestorer(const ScopedHDCRestorer &) = delete;
  ScopedHDCRestorer &operator = (const ScopedHDCRestorer &) = delete;
  ScopedHDCRestorer(ScopedHDCRestorer &&rhs) noexcept : m_hdc{rhs.m_hdc}, m_val{rhs.m_val} { rhs.release(); }
  ScopedHDCRestorer &operator = (ScopedHDCRestorer &rhs) noexcept {
    reset(rhs.release());
    return *this;
  }
};

struct SelectObjectRestorer {
  using ValueType = HGDIOBJ;
  constexpr static ValueType INVALID = nullptr;
  static void restore(HDC hdc, HGDIOBJ hobj) noexcept { SelectObject(hdc, hobj); }
};

struct SetBkModeRestorer {
  using ValueType = int;
  constexpr static ValueType INVALID = 0;
  static void restore(HDC hdc, int mode) noexcept { SetBkMode(hdc, mode); }
};

struct SetTextColorRestorer {
  using ValueType = COLORREF;
  constexpr static ValueType INVALID = CLR_INVALID;
  static void restore(HDC hdc, COLORREF color) noexcept { SetTextColor(hdc, color); }
};

using ScopedSelectObjectRestorer = ScopedHDCRestorer<SelectObjectRestorer>;
using ScopedSetBkModeRestorer = ScopedHDCRestorer<SetBkModeRestorer>;
using ScopedSetTextColorRestorer = ScopedHDCRestorer<SetTextColorRestorer>;

} // namespace Bits_

[[nodiscard]] inline Bits_::ScopedSelectObjectRestorer scoped_select_font(HDC hdc, HFONT hfont) {
  auto v = hfont ? throw_if<Win32ErrorCode, HGDIOBJ, nullptr>(SelectObject(hdc, reinterpret_cast<HGDIOBJ>(hfont))) : nullptr;
  return Bits_::ScopedSelectObjectRestorer{hdc, v};
}

[[nodiscard]] inline Bits_::ScopedSetBkModeRestorer scoped_set_bk_mode(HDC hdc, int mode) {
  auto v = mode ? throw_if<Win32ErrorCode, int, 0>(SetBkMode(hdc, mode)) : 0;
  return Bits_::ScopedSetBkModeRestorer{hdc, v};
}

[[nodiscard]] inline Bits_::ScopedSetTextColorRestorer scoped_set_text_color(HDC hdc, COLORREF color) {
  auto v = color != CLR_INVALID ? throw_if<Win32ErrorCode, COLORREF, CLR_INVALID>(SetTextColor(hdc, color)) : CLR_INVALID;
  return Bits_::ScopedSetTextColorRestorer{hdc, v};
}

} // namespace AM::Win32
