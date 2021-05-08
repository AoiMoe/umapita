#pragma once

#include "am/util.h"

namespace AM::Win32 {

using tstring = std::basic_string<TCHAR>;

inline tstring remove_ws_on_both_ends(const tstring &src) {
  auto b = std::find_if(src.begin(), src.end(), [](WCHAR c) { return !_istspace(c); });
  auto e = std::find_if(src.rbegin(), src.rend(), [](WCHAR c) { return !_istspace(c); }).base();
  return b < e ? tstring{b, e} : tstring{};
}

struct StrPtr {
  LPCTSTR ptr;
  StrPtr(std::nullptr_t) : ptr(nullptr) { }
  StrPtr(LPTSTR s) : ptr(s) { }
  StrPtr(LPCTSTR s) : ptr(s) { }
  StrPtr(const tstring &s) : ptr(s.c_str()) { }
};


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


template <typename ...Args>
inline tstring asprintf(StrPtr fmt, Args ...args) {
  return get_sz(_sntprintf(nullptr, 0, fmt.ptr, args...), [&](LPTSTR buf) { _stprintf(buf, fmt.ptr, args...); });
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
// window handle
//
namespace Bits_ {
template <typename> struct WindowConv;
class Paint;
} // namespace Bits_

class Window {
  HWND m_hWnd = nullptr;
public:
  ~Window() = default;
  Window() = default;
  Window(HWND w) : m_hWnd(w) { }
  Window(const Window &) = default;
  Window &operator = (const Window &) = default;
  void reset() noexcept { m_hWnd = nullptr; }
  HWND get() const noexcept { return m_hWnd; }
  template <typename T>
  static Window from(T v) noexcept { return Bits_::WindowConv<T>::from(v); }
  template <typename T>
  T to() const noexcept { return Bits_::WindowConv<T>::to(get()); }
  explicit operator bool () const noexcept { return !!get(); }
  tstring get_text() const {
    return Win32::get_sz(::GetWindowTextLength(get()), [=](LPTSTR buf, std::size_t len) { ::GetWindowText(get(), buf, len+1); });
  }
  void set_text(StrPtr text) const {
    SetWindowText(get(), text.ptr);
  }
  Window get_parent() const { return GetParent(get()); }
  RECT get_client_rect() const {
    RECT r;
    GetClientRect(get(), &r);
    return r;
  }
  RECT get_window_rect() const {
    RECT r;
    GetWindowRect(get(), &r);
    return r;
  }
  void invalidate_rect(const RECT &r, bool isredraw) const { InvalidateRect(get(), &r, isredraw); }
  void update_window() const { UpdateWindow(get()); }
  void set_foreground() const { SetForegroundWindow(get()); }
  void enable(bool en) const { EnableWindow(get(), en); }
  void show(int nCmdShow) const { ShowWindow(get(), nCmdShow); }
  bool is_visible() const { return IsWindowVisible(get()); }
  bool is_dialog_message(LPMSG msg) const { return IsDialogMessage(get(), msg); }
  bool translate_accelerator(HACCEL hAcc, LPMSG msg) { return TranslateAccelerator(get(), hAcc, msg); }
  void post(UINT msg, WPARAM wParam, LPARAM lParam) const{ PostMessage(get(), msg, wParam, lParam); }
  LRESULT send(UINT msg, WPARAM wParam, LPARAM lParam) const { return SendMessage(get(), msg, wParam, lParam); }
  void destroy() const { DestroyWindow(get()); }
  UINT_PTR set_timer(UINT_PTR nIDEvent, UINT uElapse, TIMERPROC lpTimerFunc) const {
    return SetTimer(get(), nIDEvent, uElapse, lpTimerFunc);
  }
  void kill_timer(UINT_PTR uIDEvent) const { KillTimer(get(), uIDEvent); }
  void set_pos(Window insertAfter, int x, int y, int cx, int cy, UINT uFlags) const {
    throw_if<Win32ErrorCode, false>(SetWindowPos(get(), insertAfter.get(), x, y, cx, cy, uFlags), "Window::set_pos");
  }
  static Window find(StrPtr cls, StrPtr name) {
    return FindWindow(cls.ptr, name.ptr);
  }
  WINDOWINFO get_info() const {
    auto wi = make_sized_pod<WINDOWINFO>();
    throw_if<Win32ErrorCode, false>(GetWindowInfo(get(), &wi), "Window::get_info");
    return wi;
  }
  HMONITOR get_monitor(DWORD dwFlags) const { return MonitorFromWindow(get(), dwFlags); }
  LONG_PTR get_long_ptr(int i) const { return GetWindowLongPtr(get(), i); }
  template <typename T>
  T get_long_ptr(int i) const { return reinterpret_cast<T>(GetWindowLongPtr(get(), i)); }
  LONG_PTR set_long_ptr(int i, LONG_PTR v) const { return SetWindowLongPtr(get(), i, v); }
  template <typename T>
  T set_long_ptr(int i, T v) const { return reinterpret_cast<T>(SetWindowLongPtr(get(), i, reinterpret_cast<LONG_PTR>(v))); }
  template <typename T>
  T get_user_data() const { return get_long_ptr<T>(GWLP_USERDATA); }
  template <typename T>
  T set_user_data(T v) const { return set_long_ptr(GWLP_USERDATA, v); }
  template <typename T>
  T get_dialog_user_data() const { return get_long_ptr<T>(DWLP_USER); }
  template <typename T>
  T set_dialog_user_data(T v) const { return set_long_ptr(DWLP_USER, v); }
  WNDPROC get_wndproc() const { return get_long_ptr<WNDPROC>(GWLP_WNDPROC); }
  WNDPROC set_wndproc(WNDPROC p) const { return set_long_ptr(GWLP_WNDPROC, p); }
  DLGPROC get_dlgproc() const { return get_long_ptr<DLGPROC>(DWLP_DLGPROC); }
  DLGPROC set_dlgproc(DLGPROC p) const { return set_long_ptr(DWLP_DLGPROC, p); }
  HINSTANCE get_instance() const { return get_long_ptr<HINSTANCE>(GWLP_HINSTANCE); }
  int message_box(StrPtr text, StrPtr caption, UINT uType) const {
    return MessageBox(get(), text.ptr, caption.ptr, uType);
  }
  // XXX: dialog
  Window get_item(int id) const {
    return GetDlgItem(get(), id);
  }
  void end_dialog(INT_PTR result) const {
    EndDialog(get(), result);
  }
  HMENU get_system_menu(bool bRevert = false) const {
    return GetSystemMenu(get(), bRevert);
  }
  std::pair<DWORD, DWORD> get_thread_process_id() const {
    DWORD tid, pid;
    tid = GetWindowThreadProcessId(get(), &pid);
    return {tid, pid};
  }
  Bits_::Paint begin_paint() const;
};

inline bool operator == (Window lhs, Window rhs) { return lhs.get() == rhs.get(); }
inline bool operator != (Window lhs, Window rhs) { return !(lhs == rhs); }

namespace Bits_ {

template <>
struct WindowConv<Window> {
  static Window from(Window w) noexcept { return w; }
  static Window to(Window w) noexcept { return w; }
};
template <>
struct WindowConv<std::nullptr_t> {
  static Window from(std::nullptr_t) noexcept { return Window{}; }
};
template <>
struct WindowConv<WPARAM> {
  static Window from(WPARAM w) noexcept { return Window{reinterpret_cast<HWND>(w)}; }
  static WPARAM to(Window w) noexcept { return reinterpret_cast<WPARAM>(w.get()); }
};
template <>
struct WindowConv<LPARAM> {
  static Window from(LPARAM w) noexcept { return Window{reinterpret_cast<HWND>(w)}; }
  static LPARAM to(Window w) noexcept { return reinterpret_cast<LPARAM>(w.get()); }
};

class Paint {
  friend Window;
  Window m_window;
  PAINTSTRUCT m_ps;
  Paint(Window w, const PAINTSTRUCT &ps) : m_window(w), m_ps(ps) { }
public:
  void reset() {
    if (m_window)
      EndPaint(m_window.get(), &m_ps);
    m_window = Window{};
  }
  ~Paint() { reset(); }
  Paint() = default;
  Paint(const Paint &) = delete;
  Paint(Paint &&rhs) {
    *this = std::move(rhs);
  }
  Paint &operator = (const Paint &) = delete;
  Paint &operator = (Paint &&rhs) {
    reset();
    m_window = rhs.m_window;
    m_ps = rhs.m_ps;
    rhs.m_window = Window{};
    return *this;
  }
  Window window() const noexcept { return m_window; }
  HDC hdc() const noexcept { return m_ps.hdc; }
  const PAINTSTRUCT &ps() const noexcept { return m_ps; }
};

} // namespace Bits_

inline Bits_::Paint Window::begin_paint() const {
  PAINTSTRUCT ps;
  BeginPaint(get(), &ps);
  return Bits_::Paint{*this, ps};
}

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

} // namespace Bits_

using Menu = std::unique_ptr<HMENU, Bits_::HMenuDeleter>;
struct MenuHandle {
  HMENU hMenu;
  MenuHandle(const Menu &m) : hMenu{m.get()} { }
  MenuHandle(HMENU m) : hMenu{m} { }
};

inline Menu create_popup_menu() {
  return Menu{CreatePopupMenu()};
}

inline Menu load_menu(HINSTANCE hInstance, LPCTSTR lpMenu) {
  return Menu{LoadMenu(hInstance, lpMenu)};
}

inline MenuHandle get_sub_menu(MenuHandle m, int n) {
  return MenuHandle{throw_if<Win32ErrorCode, HMENU, nullptr>(GetSubMenu(m.hMenu, n))};
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

//
// misc
//

//
// place popup in the center of the owner window
//
inline void center_popup(Window popup, Window owner) {
  auto rcOwner = owner.get_window_rect();
  auto rcPopup = popup.get_window_rect();
  auto w = width(rcPopup);
  auto h = height(rcPopup);
  auto x = rcOwner.left + (width(rcOwner) - w)/2;
  auto y = rcOwner.top + (height(rcOwner) - h)/2;

  // fit the screen monitor
  auto hm = owner.get_monitor(MONITOR_DEFAULTTONULL);
  if (hm) {
    auto mi = make_sized_pod<MONITORINFOEX>();
    GetMonitorInfo(hm, &mi);
    x = std::max(x, mi.rcWork.left);
    y = std::max(y, mi.rcWork.top);
    auto r = std::min(x + w, mi.rcWork.right);
    auto b = std::min(y + h, mi.rcWork.bottom);
    x = r - w;
    y = b - h;
  }

  popup.set_pos(Window{}, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

//
// open message box in the center of the owner window
//
inline int open_message_box_in_center(Window owner, StrPtr text, StrPtr caption, UINT type) {
  static AM_TLS_SPEC Window s_owner;
  static AM_TLS_SPEC HHOOK s_hHook = nullptr;

  s_owner = owner;
  s_hHook = SetWindowsHookEx(WH_CBT,
                             [](int code, WPARAM wParam, LPARAM lParam) CALLBACK {
                               HHOOK hHook = s_hHook;
                               if (code == HCBT_ACTIVATE) {
                                 UnhookWindowsHookEx(hHook);
                                 s_hHook = nullptr;
                                 center_popup(Window::from(wParam), s_owner);
                               }
                               return CallNextHookEx(hHook, code, wParam, lParam);
                             },
                             owner.get_instance(), GetCurrentThreadId());
  auto ret = owner.message_box(text.ptr, caption.ptr, type);
  s_owner.reset();
  s_hHook = nullptr;
  return ret;
}


} // namespace AM::Win32
