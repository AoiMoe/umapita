#include "pch.h"
#include "am/win32util.h"
#include "am/win32reg.h"
#include "umapita_res.h"

namespace Win32 = AM::Win32;
using AM::Log;

constexpr UINT WM_TASKTRAY = WM_USER+0x1000;
constexpr UINT TASKTRAY_ID = 1;
constexpr UINT TIMER_ID = 1;
constexpr UINT TIMER_PERIOD = 200;
constexpr TCHAR TARGET_WINDOW_CLASS[] = TEXT("UnityWndClass");
constexpr TCHAR TARGET_WINDOW_NAME[] = TEXT("umamusume");
constexpr int MIN_WIDTH = 100;
constexpr int MIN_HEIGHT = 100;

// reinterpret_cast は constexpr ではないので constexpr auto REG_ROOT_KEY = HKEY_CURRENT_USER; だと通らない
#define REG_ROOT_KEY HKEY_CURRENT_USER
constexpr TCHAR REG_PROJECT_ROOT_PATH[] = TEXT("Software\\AoiMoe\\umapita");
constexpr TCHAR REG_PROFILES_SUBKEY[] = TEXT("profiles");
constexpr auto MAX_PROFILE_NAME = 100;

struct Monitor {
  Win32::tstring name;
  RECT whole;
  RECT work;
  bool isPrimary;
public:
  Monitor(LPCTSTR aName, RECT aWhole, RECT aWork, bool aIsPrimary) : name{aName}, whole{aWhole}, work{aWork}, isPrimary{aIsPrimary} { }
};
using Monitors = std::vector<Monitor>;

HINSTANCE hInstance;
UINT msgTaskbarCreated = 0;
Win32::Icon appIcon = nullptr, appIconSm = nullptr;
Monitors monitors;
HFONT hFontMainDialog = nullptr;

//
// 監視対象ウィンドウの状態
//
struct TargetStatus {
  HWND hWnd = nullptr;
  RECT windowRect{0, 0, 0, 0};
  RECT clientRect{0, 0, 0, 0};
};
inline bool operator == (const TargetStatus &lhs, const TargetStatus &rhs) {
  using namespace AM::Win32::Op;
  return lhs.hWnd == rhs.hWnd && (!lhs.hWnd || (lhs.windowRect == rhs.windowRect && lhs.clientRect == rhs.clientRect));
}


//
// 設定値
//
struct PerOrientationSetting {
  LONG monitorNumber = 0;
  bool isConsiderTaskbar;
  enum WindowArea { Whole, Client } windowArea;
  LONG size = 0;
  enum SizeAxis { Width, Height } axis = Height;
  enum Origin { N, S, W, E, NW, NE, SW, SE, C } origin = N;
  LONG offsetX = 0, offsetY = 0;
  LONG aspectX, aspectY; // XXX: アスペクト比を固定しないと計算誤差で変な比率になることがある
};

struct Setting {
  bool isLocked = false;
  // .xxx=xxx 記法は ISO C++20 からだが、gcc なら使えるのでヨシ！
  PerOrientationSetting verticalSetting{
    .isConsiderTaskbar = true,
    .windowArea = PerOrientationSetting::Whole,
    .aspectX=9,
    .aspectY=16
  };
  PerOrientationSetting horizontalSetting{
    .isConsiderTaskbar = false,
    .windowArea = PerOrientationSetting::Client,
    .aspectX=16,
    .aspectY=9
  };
};

constexpr Setting DEFAULT_SETTING{};

struct GlobalSetting {
  bool isEnabled = true;
  Setting currentProfile{DEFAULT_SETTING};
};

constexpr GlobalSetting DEFAULT_GLOBAL_SETTING{};

GlobalSetting s_currentGlobalSetting{DEFAULT_GLOBAL_SETTING};

//
// ダイアログボックス上のコントロールと設定のマッピング
//
template <typename Enum>
struct CheckButtonMap {
  Enum unchecked;
  Enum checked;
  int id;
};

constexpr CheckButtonMap<bool> make_bool_check_button_map(int id) {
  return CheckButtonMap<bool>{false, true, id};
}

template <typename Enum, std::size_t Num>
using RadioButtonMap = std::array<std::pair<Enum, int>, Num>;

struct SelectMonitorMap {
  int id;
  int base;
};

struct PerOrientationSettingID {
  int monitorNumber;
  CheckButtonMap<bool> isConsiderTaskbar;
  RadioButtonMap<PerOrientationSetting::WindowArea, 2> windowArea;
  int size;
  RadioButtonMap<PerOrientationSetting::SizeAxis, 2> axis;
  RadioButtonMap<PerOrientationSetting::Origin, 9> origin;
  int offsetX, offsetY;
  SelectMonitorMap selectMonitor;
};

constexpr PerOrientationSettingID verticalSettingID = {
  // monitorNumber
  IDC_V_MONITOR_NUMBER,
  // isConsiderTaskbar
  make_bool_check_button_map(IDC_V_IS_CONSIDER_TASKBAR),
  // windowArea
  {{{PerOrientationSetting::Whole, IDC_V_WHOLE_AREA},
    {PerOrientationSetting::Client, IDC_V_CLIENT_AREA}}},
  // size
  IDC_V_SIZE,
  // axis
  {{{PerOrientationSetting::Width, IDC_V_AXIS_WIDTH},
    {PerOrientationSetting::Height, IDC_V_AXIS_HEIGHT}}},
  // origin
  {{{PerOrientationSetting::N, IDC_V_ORIGIN_N},
    {PerOrientationSetting::S, IDC_V_ORIGIN_S},
    {PerOrientationSetting::W, IDC_V_ORIGIN_W},
    {PerOrientationSetting::E, IDC_V_ORIGIN_E},
    {PerOrientationSetting::NW, IDC_V_ORIGIN_NW},
    {PerOrientationSetting::NE, IDC_V_ORIGIN_NE},
    {PerOrientationSetting::SW, IDC_V_ORIGIN_SW},
    {PerOrientationSetting::SE, IDC_V_ORIGIN_SE},
    {PerOrientationSetting::C, IDC_V_ORIGIN_C}}},
  // offsetX
  IDC_V_OFFSET_X,
  // offsetY
  IDC_V_OFFSET_Y,
  // selectMonitor
  {IDC_V_SELECT_MONITORS, IDM_V_MONITOR_BASE},
};
constexpr PerOrientationSettingID horizontalSettingID = {
  // monitorNumber
  IDC_H_MONITOR_NUMBER,
  // isConsiderTaskbar
  make_bool_check_button_map(IDC_H_IS_CONSIDER_TASKBAR),
  // windowArea
  {{{PerOrientationSetting::Whole, IDC_H_WHOLE_AREA},
    {PerOrientationSetting::Client, IDC_H_CLIENT_AREA}}},
  // size
  IDC_H_SIZE,
  // axis
  {{{PerOrientationSetting::Width, IDC_H_AXIS_WIDTH},
    {PerOrientationSetting::Height, IDC_H_AXIS_HEIGHT}}},
  // origin
  {{{PerOrientationSetting::N, IDC_H_ORIGIN_N},
    {PerOrientationSetting::S, IDC_H_ORIGIN_S},
    {PerOrientationSetting::W, IDC_H_ORIGIN_W},
    {PerOrientationSetting::E, IDC_H_ORIGIN_E},
    {PerOrientationSetting::NW, IDC_H_ORIGIN_NW},
    {PerOrientationSetting::NE, IDC_H_ORIGIN_NE},
    {PerOrientationSetting::SW, IDC_H_ORIGIN_SW},
    {PerOrientationSetting::SE, IDC_H_ORIGIN_SE},
    {PerOrientationSetting::C, IDC_H_ORIGIN_C}}},
  // offsetX
  IDC_H_OFFSET_X,
  // offsetY
  IDC_H_OFFSET_Y,
  // selectMonitor
  {IDC_H_SELECT_MONITORS, IDM_H_MONITOR_BASE},
};


//
// カスタムグループボックス
//
// WndProc をオーバライドしていくつかの WM を置き換える
//
class CustomGroupBox {
  HWND m_hWnd = nullptr;
  WNDPROC m_lpPrevWndFunc = nullptr;
  bool m_isSelected = false;
  //
  static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto self = reinterpret_cast<CustomGroupBox *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT:
      AM::try_or_void([self, hWnd]() { self->on_paint(hWnd); });
      return 0;
    case WM_GETDLGCODE:
      return DLGC_STATIC;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    }
    return CallWindowProc(self->m_lpPrevWndFunc, hWnd, msg, wParam, lParam);
  }
  void on_paint(HWND hWnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    // テキスト描画
    auto scopedSelect = Win32::scoped_select_font(hdc, hFontMainDialog);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);

    auto text = Win32::get_window_text(hWnd);
    int len = text.size();

    SIZE size;
    GetTextExtentPoint32(hdc, text.c_str(), len, &size);

    {
      auto scopedBkMode = Win32::scoped_set_bk_mode(hdc, TRANSPARENT);
      auto scopedTextColor = Win32::scoped_set_text_color(hdc, GetSysColor(COLOR_WINDOWTEXT));
      TextOut(hdc, tm.tmAveCharWidth*5/4, 0, text.c_str(), len);
    }

    // 枠描画
    RECT rect;
    GetClientRect(hWnd, &rect);
    if (len) {
      // テキスト部分を描画エリアから除外する
      auto r = Win32::create_rect_region(tm.tmAveCharWidth, 0, tm.tmAveCharWidth*3/2 + size.cx, size.cy);
      ExtSelectClipRgn(hdc, r.get(), RGN_DIFF);
    }
    {
      // 選択状態のときは黒くて幅 2 のラインを、非選択状態のときは灰色で幅 1 のラインを描く
      auto r = Win32::create_rect_region(rect.left, rect.top+size.cy/2, rect.right, rect.bottom);
      auto hBrush = reinterpret_cast<HBRUSH>(GetStockObject(m_isSelected ? BLACK_BRUSH : LTGRAY_BRUSH));
      auto w = m_isSelected ? 2 : 1;
      FrameRgn(hdc, r.get(), hBrush, w, w);
    }
    SelectClipRgn(hdc, nullptr);

    EndPaint(hWnd, &ps);
  }
  void redraw() {
    if (m_hWnd) {
      auto hWndParent = GetParent(m_hWnd);
      RECT rect;
      GetWindowRect(m_hWnd, &rect);
      MapWindowPoints(HWND_DESKTOP, hWndParent, reinterpret_cast<LPPOINT>(&rect), 2);
      InvalidateRect(hWndParent, &rect, true);
      UpdateWindow(hWndParent);
    }
  }
public:
  CustomGroupBox() { }
  void override_window_proc(HWND hWnd) {
    m_hWnd = hWnd;
    m_lpPrevWndFunc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(hWnd, GWLP_WNDPROC));
    SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc));
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    redraw();
  }
  void restore_window_proc() {
    if (m_hWnd) {
      SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_lpPrevWndFunc));
      SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
      m_hWnd = nullptr;
      m_lpPrevWndFunc = nullptr;
    }
  }
  void set_selected(bool isSelected) {
    if ((m_isSelected && !isSelected) || (!m_isSelected && isSelected)) {
      m_isSelected = isSelected;
      redraw();
    }
  }
};
CustomGroupBox s_verticalGroupBox, s_horizontalGroupBox;


//
// レジストリ
//

template <typename Map>
struct RegMap {
  using ValueType = typename Map::ValueType;
  LPCTSTR name;
  Map typeMap;
  ValueType defaultValue;
};

template <typename Enum, std::size_t Num>
struct RegEnumMap {
  using ValueType = Enum;
  std::array<std::pair<LPCTSTR, Enum>, Num> enumMap;
};

struct RegBoolMap {
  using ValueType = bool;
};

struct RegLongMap {
  using ValueType = LONG;
};

struct PerOrientationSettingRegMap {
  using WindowAreaMap = RegEnumMap<PerOrientationSetting::WindowArea, 2>;
  constexpr static WindowAreaMap WindowArea = {{{
        {TEXT("Whole"), PerOrientationSetting::Whole},
        {TEXT("Client"), PerOrientationSetting::Client},
      }}};
  using SizeAxisMap = RegEnumMap<PerOrientationSetting::SizeAxis, 2>;
  constexpr static SizeAxisMap SizeAxis = {{{
        {TEXT("Width"), PerOrientationSetting::Width},
        {TEXT("Height"), PerOrientationSetting::Height},
      }}};
  using OriginMap = RegEnumMap<PerOrientationSetting::Origin, 9>;
  constexpr static OriginMap Origin = {{{
        {TEXT("N"), PerOrientationSetting::N},
        {TEXT("S"), PerOrientationSetting::S},
        {TEXT("W"), PerOrientationSetting::W},
        {TEXT("E"), PerOrientationSetting::E},
        {TEXT("NW"), PerOrientationSetting::NW},
        {TEXT("NE"), PerOrientationSetting::NE},
        {TEXT("SW"), PerOrientationSetting::SW},
        {TEXT("SE"), PerOrientationSetting::SE},
      }}};
  //
  RegMap<RegLongMap> monitorNumber;
  RegMap<RegBoolMap> isConsiderTaskbar;
  RegMap<WindowAreaMap> windowArea;
  RegMap<RegLongMap> size;
  RegMap<SizeAxisMap> axis;
  RegMap<OriginMap> origin;
  RegMap<RegLongMap> offsetX;
  RegMap<RegLongMap> offsetY;
  RegMap<RegLongMap> aspectX;
  RegMap<RegLongMap> aspectY;
};

constexpr PerOrientationSettingRegMap verticalSettingRegMap = {
  {TEXT("vMonitorNumber"), {}, DEFAULT_SETTING.verticalSetting.monitorNumber},
  {TEXT("vIsConsiderTaskbar"), {}, DEFAULT_SETTING.verticalSetting.isConsiderTaskbar},
  {TEXT("vWindowArea"), PerOrientationSettingRegMap::WindowArea, DEFAULT_SETTING.verticalSetting.windowArea},
  {TEXT("vSize"), {}, DEFAULT_SETTING.verticalSetting.size},
  {TEXT("vSizeAxis"), PerOrientationSettingRegMap::SizeAxis, DEFAULT_SETTING.verticalSetting.axis},
  {TEXT("vOrigin"), PerOrientationSettingRegMap::Origin, DEFAULT_SETTING.verticalSetting.origin},
  {TEXT("vOffsetX"), {}, DEFAULT_SETTING.verticalSetting.offsetX},
  {TEXT("vOffsetY"), {}, DEFAULT_SETTING.verticalSetting.offsetY},
  {TEXT("vAspectX"), {}, DEFAULT_SETTING.verticalSetting.aspectX},
  {TEXT("vAspectY"), {}, DEFAULT_SETTING.verticalSetting.aspectY},
};

constexpr PerOrientationSettingRegMap horizontalSettingRegMap = {
  {TEXT("hMonitorNumber"), {}, DEFAULT_SETTING.horizontalSetting.monitorNumber},
  {TEXT("hIsConsiderTaskbar"), {}, DEFAULT_SETTING.horizontalSetting.isConsiderTaskbar},
  {TEXT("hWindowArea"), PerOrientationSettingRegMap::WindowArea, DEFAULT_SETTING.horizontalSetting.windowArea},
  {TEXT("hSize"), {}, DEFAULT_SETTING.horizontalSetting.size},
  {TEXT("hSizeAxis"), PerOrientationSettingRegMap::SizeAxis, DEFAULT_SETTING.horizontalSetting.axis},
  {TEXT("hOrigin"), PerOrientationSettingRegMap::Origin, DEFAULT_SETTING.horizontalSetting.origin},
  {TEXT("hOffsetX"), {}, DEFAULT_SETTING.horizontalSetting.offsetX},
  {TEXT("hOffsetY"), {}, DEFAULT_SETTING.horizontalSetting.offsetY},
  {TEXT("hAspectX"), {}, DEFAULT_SETTING.horizontalSetting.aspectX},
  {TEXT("hAspectY"), {}, DEFAULT_SETTING.horizontalSetting.aspectY},
};

constexpr RegMap<RegBoolMap> isLockedRegMap = {TEXT("isLocked"), RegBoolMap{}, DEFAULT_SETTING.isLocked};
constexpr RegMap<RegBoolMap> isEnabledRegMap = {TEXT("isEnabled"), RegBoolMap{}, DEFAULT_GLOBAL_SETTING.isEnabled};


struct RegGetFailed : AM::RuntimeError<RegGetFailed> { };

inline bool reg_get_(const Win32::Reg::Key &key, const RegMap<RegBoolMap> &m) {
  bool v = !!Win32::Reg::query_dword(key, m.name);
  Log::debug(TEXT("reg_get: %S -> %hs"), m.name, v ? "true":"false");
  return v;
}

inline LONG reg_get_(const Win32::Reg::Key &key, const RegMap<RegLongMap> &m) {
  LONG v = static_cast<INT32>(Win32::Reg::query_dword(key, m.name));
  Log::debug(TEXT("reg_get: %S -> %ld"), m.name, v);
  return v;
}

template <class Enum, std::size_t Num>
Enum reg_get_(const Win32::Reg::Key &key, const RegMap<RegEnumMap<Enum, Num>> &m) {
  auto str = Win32::Reg::query_sz(key, m.name);
  Log::debug(TEXT("reg_get: %S -> %S"), m.name, str.c_str());
  for (auto [t, v] : m.typeMap.enumMap) {
    if (str == t)
      return v;
  }
  Log::error(TEXT("unknown enum tag in \"%S\": %S"), m.name, str.c_str());
  throw RegGetFailed{};
}

template <class T>
auto reg_get(const Win32::Reg::Key &key, const RegMap<T> &m) {
  try {
    return reg_get_(key, m);
  }
  catch (Win32::Reg::ErrorCode &ex) {
    if (ex.code == ERROR_FILE_NOT_FOUND) {
      Log::warning(TEXT("\"%S\" is not found - use default value"), m.name);
      return m.defaultValue;
    }
    Log::error(TEXT("cannot get \"%S\" value: %hs(%d)"), m.name, ex.what(), ex.code);
    throw RegGetFailed{};
  }
}

struct RegPutFailed : AM::RuntimeError<RegGetFailed> { };

inline void reg_put_(const Win32::Reg::Key &key, const RegMap<RegBoolMap> &m, bool v) {
  Log::debug(TEXT("reg_put: %S -> %hs"), m.name, v ? "true":"false");
  Win32::Reg::set_dword(key, m.name, static_cast<DWORD>(v));
}

inline void reg_put_(const Win32::Reg::Key &key, const RegMap<RegLongMap> &m, LONG v) {
  Log::debug(TEXT("reg_put: %S -> %ld"), m.name, v);
  Win32::Reg::set_dword(key, m.name, static_cast<DWORD>(v));
}

template <class Enum, std::size_t Num>
inline void reg_put_(const Win32::Reg::Key &key, const RegMap<RegEnumMap<Enum, Num>> &m, Enum v) {
  LPCTSTR tag = [&m,v]() {
                  for (auto [t, x] : m.typeMap.enumMap) {
                    if (v == x)
                      return t;
                  }
                  throw RegPutFailed{};
                }();
  Log::debug(TEXT("reg_put: %S -> %S"), m.name, tag);
  Win32::Reg::set_sz(key, m.name, tag);
}

template <class T, class V>
auto reg_put(const Win32::Reg::Key &key, const RegMap<T> &m, const V &v) {
  try {
    return reg_put_(key, m, v);
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::error(TEXT("cannot put \"%S\" value: %hs(%d)"), m.name, ex.what(), ex.code);
    throw RegPutFailed{};
  }
}

inline Win32::tstring make_regpath(LPCTSTR profileName) {
  Win32::tstring tmp{REG_PROJECT_ROOT_PATH};

  if (profileName) {
    tmp += TEXT("\\");
    tmp += REG_PROFILES_SUBKEY;
    tmp += TEXT("\\");
    tmp += profileName;
  }
  return tmp;
}

inline PerOrientationSetting reg_get_per_orientation_setting(const Win32::Reg::Key &key, PerOrientationSettingRegMap m) {
  return PerOrientationSetting{
    reg_get(key, m.monitorNumber),
    reg_get(key, m.isConsiderTaskbar),
    reg_get(key, m.windowArea),
    reg_get(key, m.size),
    reg_get(key, m.axis),
    reg_get(key, m.origin),
    reg_get(key, m.offsetX),
    reg_get(key, m.offsetY),
    reg_get(key, m.aspectX),
    reg_get(key, m.aspectY),
  };
}

inline void reg_put_per_orientation_setting(const Win32::Reg::Key &key, PerOrientationSettingRegMap m, const PerOrientationSetting s) {
  reg_put(key, m.monitorNumber, s.monitorNumber);
  reg_put(key, m.isConsiderTaskbar, s.isConsiderTaskbar);
  reg_put(key, m.windowArea, s.windowArea);
  reg_put(key, m.size, s.size);
  reg_put(key, m.axis, s.axis);
  reg_put(key, m.origin, s.origin);
  reg_put(key, m.offsetX, s.offsetX);
  reg_put(key, m.offsetY, s.offsetY);
  reg_put(key, m.aspectX, s.aspectX);
  reg_put(key, m.aspectY, s.aspectY);
}

static Setting load_setting(LPCTSTR profileName) {
  auto path = make_regpath(profileName);

  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path.c_str(), 0, KEY_READ);
    try {
      return Setting{
        reg_get(key, isLockedRegMap),
        reg_get_per_orientation_setting(key, verticalSettingRegMap),
        reg_get_per_orientation_setting(key, horizontalSettingRegMap),
      };
    }
    catch (RegGetFailed &) {
      return DEFAULT_SETTING;
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
    return DEFAULT_SETTING;
  }
}

static void save_setting(LPCTSTR profileName, const Setting &s) {
  auto path = make_regpath(profileName);

  try {
    [[maybe_unused]] auto [key, disp] = Win32::Reg::create_key(REG_ROOT_KEY, path.c_str(), 0, KEY_WRITE);
    try {
      reg_put(key, isLockedRegMap, s.isLocked);
      reg_put_per_orientation_setting(key, verticalSettingRegMap, s.verticalSetting);
      reg_put_per_orientation_setting(key, horizontalSettingRegMap, s.horizontalSetting);
    }
    catch (RegPutFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
}

static GlobalSetting load_global_setting() {
  auto path = make_regpath(nullptr);
  auto isEnabled = DEFAULT_GLOBAL_SETTING.isEnabled;
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path.c_str(), 0, KEY_READ);
    try {
      isEnabled = reg_get(key, isEnabledRegMap);
    }
    catch (RegGetFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
  return GlobalSetting{isEnabled, load_setting(nullptr)};
}

static void save_global_setting(const GlobalSetting &s) {
  auto path = make_regpath(nullptr);

  try {
    [[maybe_unused]] auto [key, disp ] = Win32::Reg::create_key(REG_ROOT_KEY, path.c_str(), 0, KEY_WRITE);
    try {
      reg_put(key, isEnabledRegMap, s.isEnabled);
    }
    catch (RegPutFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
  save_setting(nullptr, s.currentProfile);
}


//
// UI
//

inline NOTIFYICONDATA make_notify_icon_data(HWND hWnd, UINT uID) {
  auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
  nid.hWnd = hWnd;
  nid.uID = uID;
  return nid;
}

static BOOL add_tasktray_icon(HWND hWnd, HICON hIcon) {
  auto nid = make_notify_icon_data(hWnd, TASKTRAY_ID);

  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TASKTRAY;
  nid.hIcon = hIcon;
  LoadString(hInstance, IDS_TASKTRAY_TIP, nid.szTip, std::size(nid.szTip));
  return Shell_NotifyIcon(NIM_ADD, &nid);
}

static void delete_tasktray_icon(HWND hWnd) {
  auto nid = make_notify_icon_data(hWnd, TASKTRAY_ID);

  Shell_NotifyIcon(NIM_DELETE, &nid);
}

static void show_popup_menu(HWND hWnd, BOOL isTray = FALSE) {
  POINT point;
  TPMPARAMS tpmp, *pTpmp = nullptr;

  if (isTray) {
    RECT rect = { 0, 0, 0, 0 };
    HWND hwndShell = FindWindow(TEXT("Shell_TrayWnd"), nullptr);
    if (hwndShell) {
      GetWindowRect(hwndShell, &rect);
      tpmp = Win32::make_sized_pod<TPMPARAMS>();
      tpmp.rcExclude = rect;
      pTpmp = &tpmp;
    }
  }

  GetCursorPos(&point);
  SetForegroundWindow(hWnd);

  auto menu = Win32::load_menu(hInstance, MAKEINTRESOURCE(IDM_POPUP));
  auto submenu = Win32::get_sub_menu(menu, 0);
  TrackPopupMenuEx(submenu.get(), TPM_LEFTALIGN | TPM_LEFTBUTTON, point.x, point.y, hWnd, pTpmp);
}

static BOOL update_monitors_callback(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
  Monitors &ms = *reinterpret_cast<Monitors *>(lParam);
  auto mi = Win32::make_sized_pod<MONITORINFOEX>();
  GetMonitorInfo(hMonitor, &mi);
  Log::debug(TEXT("hMonitor=%p, szDevice=%S, rcMonitor=(%ld,%ld)-(%ld,%ld), rcWork=(%ld,%ld)-(%ld,%ld), dwFlags=%X"),
             hMonitor, mi.szDevice,
             mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
             mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom,
             mi.dwFlags);
  ms.emplace_back(mi.szDevice, mi.rcMonitor, mi.rcWork, !!(mi.dwFlags & MONITORINFOF_PRIMARY));
  return TRUE;
}

static void update_monitors() {
  Monitors ret;
  EnumDisplayMonitors(nullptr, nullptr, &update_monitors_callback, reinterpret_cast<LPARAM>(&ret));

  RECT whole;
  whole.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  whole.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  whole.right = whole.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  whole.bottom = whole.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  // 0: primary monitor
  {
    if (auto result = std::find_if(ret.begin(), ret.end(), [](auto const &m) { return m.isPrimary; }); result == ret.end()) {
      // not found
      ret.emplace(ret.begin(), TEXT("<primary>"), whole, whole, false);  // 0 番の primary は isPrimary = true とはしない。
    } else {
      ret.emplace(ret.begin(), TEXT("<primary>"), result->whole, result->work, false);
    }
  }
  // -1: whole virtual desktop
  {
    ret.emplace(ret.begin(), TEXT("<all monitors>"), whole, whole, false);
  }
  monitors = std::move(ret);
}

static Win32::Menu create_monitors_menu(int idbase) {
  auto menu = Win32::create_popup_menu();
  int id = idbase;
  int index = -1;

  for ([[maybe_unused]] auto const &[name, whole, work, isPrimary] : monitors) {
    TCHAR tmp[1024];
    _stprintf(tmp, TEXT("%2d: (%6ld,%6ld)-(%6ld,%6ld) %ls"),
              index++, whole.left, whole.top, whole.right, whole.bottom, name.c_str());
    AppendMenu(menu.get(), MF_STRING, id++, tmp);
  }

  return menu;
}

template <typename MenuType>
void show_button_menu(HWND hWnd, const MenuType &menu) {
  auto tpmp = Win32::make_sized_pod<TPMPARAMS>();
  RECT rect{0, 0, 0, 0};
  GetWindowRect(hWnd, &rect);
  tpmp.rcExclude = rect;
  TrackPopupMenuEx(menu.get(), TPM_LEFTALIGN | TPM_LEFTBUTTON, rect.right, rect.top, GetParent(hWnd), &tpmp);
}

static const Monitor *get_current_monitor(int monitorNumber) {
  auto mn = monitorNumber + 1;

  if (mn < 0 || static_cast<size_t>(mn) >= monitors.size())
    return NULL;

  return &monitors[mn];
}

static TargetStatus get_target_information() {
  if (auto hWndTarget = FindWindow(TARGET_WINDOW_CLASS, TARGET_WINDOW_NAME); hWndTarget) {
    auto wi = Win32::make_sized_pod<WINDOWINFO>();
    if (GetWindowInfo(hWndTarget, &wi))
      return {hWndTarget, wi.rcWindow, wi.rcClient};
  }
  return {};
}

struct AdjustTargetResult {
  bool isChanged;
  TargetStatus targetStatus;
};

static AdjustTargetResult adjust_target(HWND hWndDialog, bool isSettingChanged) {
  static TargetStatus lastTargetStatus;
  auto const &setting = s_currentGlobalSetting;
  auto const &profile = setting.currentProfile;

  // ターゲット情報の更新
  TargetStatus ts = get_target_information();

  // ターゲットも設定も変更されていない場合は終了
  if (!isSettingChanged && ts == lastTargetStatus)
    return {false, {}};

  lastTargetStatus = ts;

  if (setting.isEnabled && ts.hWnd && IsWindowVisible(ts.hWnd)) {
    // ターゲットのジオメトリを更新する
    auto cW = Win32::width(ts.clientRect);
    auto cH = Win32::height(ts.clientRect);
    auto wW = Win32::width(ts.windowRect);
    auto wH = Win32::height(ts.windowRect);
    // ncX, ncY : クライアント領域の左上端を原点とした非クライアント領域の左上端（一般に負）
    // ncW, ncH : クライアント領域の占める幅と高さ（両サイドの和）
    auto ncX = ts.windowRect.left - ts.clientRect.left;
    auto ncY = ts.windowRect.top - ts.clientRect.top;
    auto ncW = wW - cW;
    auto ncH = wH - cH;
    const PerOrientationSetting &s = cW > cH ? profile.horizontalSetting : profile.verticalSetting;

    auto pMonitor = get_current_monitor(s.monitorNumber);
    if (!pMonitor) {
      Log::warning(TEXT("invalid monitor number: %d"), s.monitorNumber);
      return {true, lastTargetStatus};
    }

    auto const & mR = s.isConsiderTaskbar ? pMonitor->work : pMonitor->whole;
    auto [mW, mH] = Win32::extent(mR);

    // idealCW, idealCH : 理想のクライアント領域サイズ
    // 縦横比は s.windowArea の設定に関係なくクライアント領域の縦横比で固定されるため、
    // ひとまず s.size をクライアント領域のサイズに換算してクライアント領域の W, H を求める
    LONG idealCW = 0, idealCH = 0;
    switch (s.axis) {
    case PerOrientationSetting::Width: {
      // 幅方向でサイズ指定
      // - s.size が正ならウィンドウの幅を s.size にする
      // - s.size が 0 ならウィンドウの幅を画面幅に合わせる
      // - s.size が負ならウィンドウの幅を画面の幅から abs(s.size) を引いた値にする
      auto sz = s.size > 0 ? s.size : mW + s.size;
      idealCW = s.windowArea == PerOrientationSetting::Client ? sz : sz - ncW;
      idealCH = s.aspectY * idealCW / s.aspectX;
      break;
    }
    case PerOrientationSetting::Height: {
      // 高さ方向でサイズ指定
      // s.size の符号については同上
      auto sz = s.size > 0 ? s.size : mH + s.size;
      idealCH = s.windowArea == PerOrientationSetting::Client ? sz : sz - ncH;
      idealCW = s.aspectX * idealCH / s.aspectY;
      break;
    }
    }

    // 原点に対してウィンドウを配置する
    // idealX, idealY, idealW, idealH : s.windowArea の設定により、ウィンドウ領域またはクライアント領域の座標値
    LONG idealX = 0, idealY = 0;
    auto idealW = s.windowArea == PerOrientationSetting::Client ? idealCW : idealCW + ncW;
    auto idealH = s.windowArea == PerOrientationSetting::Client ? idealCH : idealCH + ncH;
    switch (s.origin) {
    case PerOrientationSetting::NW:
    case PerOrientationSetting::W:
    case PerOrientationSetting::SW:
      idealX = mR.left + s.offsetX;
      break;
    case PerOrientationSetting::C:
    case PerOrientationSetting::N:
    case PerOrientationSetting::S:
      idealX = mR.left + mW/2 - idealW/2  + s.offsetX;
      break;
    case PerOrientationSetting::NE:
    case PerOrientationSetting::E:
    case PerOrientationSetting::SE:
      idealX = mR.right - idealW - s.offsetX;
      break;
    }
    switch (s.origin) {
    case PerOrientationSetting::NW:
    case PerOrientationSetting::N:
    case PerOrientationSetting::NE:
      idealY = mR.top + s.offsetY;
      break;
    case PerOrientationSetting::C:
    case PerOrientationSetting::W:
    case PerOrientationSetting::E:
      idealY = mR.top + mH/2 - idealH/2 + s.offsetY;
      break;
    case PerOrientationSetting::SW:
    case PerOrientationSetting::S:
    case PerOrientationSetting::SE:
      idealY = mR.bottom - idealH - s.offsetY;
      break;
    }

    // idealX, idealY, idealW, idealH をウィンドウ全体領域に換算する
    if (s.windowArea == PerOrientationSetting::Client) {
      idealX += ncX;
      idealY += ncY;
      idealW += ncW;
      idealH += ncH;
    }
    // idealCX, idealCY : クライアント領域の左上の座標値を計算する
    auto idealCX = idealX - ncX;
    auto idealCY = idealY - ncY;
    Log::debug(TEXT("%p, x=%ld, y=%ld, w=%ld, h=%ld"), ts.hWnd, idealX, idealY, idealW, idealH);
    if ((idealX != ts.windowRect.left || idealY != ts.windowRect.top || idealW != wW || idealH != wH) &&
        idealW > MIN_WIDTH && idealH > MIN_HEIGHT) {
      auto willingToUpdate = true;
      if (!SetWindowPos(ts.hWnd, nullptr, idealX, idealY, idealW, idealH, SWP_NOACTIVATE | SWP_NOZORDER)) {
        // SetWindowPos に失敗
        auto err = GetLastError();
        Log::error(TEXT("SetWindowPos failed: %lu\n"), err);
        if (err == ERROR_ACCESS_DENIED) {
          // 権限がない場合、どうせ次も失敗するので lastTargetStatus を ts のままにしておく。
          // これで余計な更新が走らなくなる。
          willingToUpdate = false;
        }
      }
      if (willingToUpdate) {
        lastTargetStatus.windowRect = RECT{idealX, idealY, idealX+idealW, idealY+idealH};
        lastTargetStatus.clientRect = RECT{idealCX, idealCY, idealCX+idealCW, idealCY+idealCH};
      }
    }
  }
  return {true, lastTargetStatus};
}


//
// main dialog
//
using HandlerResult = std::pair<bool, INT_PTR>;
using Handler = std::function<HandlerResult (HWND, UINT, WPARAM, LPARAM)>;
using HandlerMap = std::unordered_map<int, Handler>;

static bool isDialogChanged = false;
static HandlerMap s_handlerMap;

template <typename Enum, std::size_t Num>
void register_handler_map(HandlerMap &hm, const RadioButtonMap<Enum, Num> &m, Handler h) {
  for (auto const &[tag, id] : m)
    hm.emplace(id, h);
}

template <typename Enum, std::size_t Num>
static void set_radio_buttons(HWND hWnd, const RadioButtonMap<Enum, Num> &m, Enum v) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  for (auto const &[tag, id] : m)
    if (tag == v)
      Button_SetCheck(get(id), BST_CHECKED);
    else
      Button_SetCheck(get(id), BST_UNCHECKED);
}

template <typename Enum>
void register_handler_map(HandlerMap &hm, const CheckButtonMap<Enum> &m, Handler h) {
  hm.emplace(m.id, h);
}

template <typename Enum>
static void set_check_button(HWND hWnd, const CheckButtonMap<Enum> &m, Enum v) {
  Button_SetCheck(GetDlgItem(hWnd, m.id), m.checked == v ? BST_CHECKED : BST_UNCHECKED);
}

void register_handler_map(HandlerMap &hm, int id, Handler h) {
  hm.emplace(id, h);
}

void register_handler_map(HandlerMap &hm, int id, std::function<HandlerResult (HWND)> h) {
  register_handler_map(hm, id, [h](HWND hWnd, UINT, WPARAM, LPARAM) { return h(hWnd); });
}

static void set_monitor_number(HWND hWnd, int id, int num) {
  TCHAR buf[256];
  _stprintf(buf, TEXT("%d"), num);
  SetWindowText(GetDlgItem(hWnd, id), buf);
}

static Handler make_long_integer_box_handler(LONG &stor) {
  return [&stor](HWND, UINT, WPARAM wParam, LPARAM lParam) {
           switch (HIWORD(wParam)) {
           case EN_CHANGE: {
             int id = LOWORD(wParam);
             TCHAR buf[256];
             GetWindowText(reinterpret_cast<HWND>(lParam), buf, std::size(buf));
             LONG val = _tcstol(buf, nullptr, 10);
             if (val != stor) {
               Log::debug(TEXT("text box %X changed: %d -> %d"), id, stor, val);
               stor = val;
               isDialogChanged = true;
             }
             return HandlerResult{true, TRUE};
           }
           }
           return HandlerResult{true, FALSE};
         };
}

template <typename Enum>
Handler make_check_button_handler(const CheckButtonMap<Enum> &m, Enum &stor) {
  return [&m, &stor](HWND, UINT, WPARAM wParam, LPARAM lParam) {
           switch (HIWORD(wParam)) {
           case BN_CLICKED: {
             int id = LOWORD(wParam);
             auto val = Button_GetCheck(reinterpret_cast<HWND>(lParam)) == BST_CHECKED ? m.checked : m.unchecked;
             if (val != stor) {
               Log::debug(TEXT("check box %X changed: %d -> %d"), id, static_cast<int>(stor) , static_cast<int>(val));
               stor = val;
               isDialogChanged = true;
             }
             return HandlerResult{true, TRUE};
           }
           }
           return HandlerResult{true, FALSE};
         };
}

template <typename Enum, std::size_t Num>
Handler make_radio_button_map(const RadioButtonMap<Enum, Num> &m, Enum &stor) {
  return [&m, &stor](HWND, UINT, WPARAM wParam, LPARAM lParam) {
           switch (HIWORD(wParam)) {
           case BN_CLICKED: {
             int cid = LOWORD(wParam);
             for (auto const &[tag, id] : m) {
               if (id == cid && tag != stor) {
                 Log::debug(TEXT("radio button %X changed: %d -> %d"), cid, static_cast<int>(stor), static_cast<int>(tag));
                 stor = tag;
                 isDialogChanged = true;
                 return HandlerResult{true, TRUE};
               }
             }
             Log::warning(TEXT("BN_CLICKED to the unknown radio button %X is received"), cid);
             return HandlerResult{true, FALSE};
           }
           }
           return HandlerResult{true, FALSE};
         };
}

template <typename MenuFactory>
Handler make_menu_button_handler(int id, MenuFactory f) {
  return [id, f](HWND, UINT, WPARAM wParam, LPARAM lParam) {
           switch (HIWORD(wParam)) {
           case BN_CLICKED: {
             [[maybe_unused]] auto [housekeeper, menu] = f();
             show_button_menu(reinterpret_cast<HWND>(lParam), menu);
             return HandlerResult{true, TRUE};
           }
           }
           return HandlerResult{true, FALSE};
         };
}

static void init_per_orientation_settings(HWND hWnd, const PerOrientationSettingID &ids, PerOrientationSetting &setting) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  auto setint = [get](auto id, int v) {
                  TCHAR buf[256];
                  _stprintf(buf, TEXT("%d"), v);
                  SetWindowText(get(id), buf);
                };

  register_handler_map(s_handlerMap, ids.monitorNumber, make_long_integer_box_handler(setting.monitorNumber));
  setint(ids.monitorNumber, setting.monitorNumber);

  register_handler_map(s_handlerMap, ids.isConsiderTaskbar, make_check_button_handler(ids.isConsiderTaskbar, setting.isConsiderTaskbar));
  set_check_button(hWnd, ids.isConsiderTaskbar, setting.isConsiderTaskbar);

  register_handler_map(s_handlerMap, ids.windowArea, make_radio_button_map(ids.windowArea, setting.windowArea));
  set_radio_buttons(hWnd, ids.windowArea, setting.windowArea);

  register_handler_map(s_handlerMap, ids.size, make_long_integer_box_handler(setting.size));
  setint(ids.size, setting.size);

  register_handler_map(s_handlerMap, ids.axis, make_radio_button_map(ids.axis, setting.axis));
  set_radio_buttons(hWnd, ids.axis, setting.axis);

  register_handler_map(s_handlerMap, ids.origin, make_radio_button_map(ids.origin, setting.origin));
  set_radio_buttons(hWnd, ids.origin, setting.origin);

  register_handler_map(s_handlerMap, ids.offsetX, make_long_integer_box_handler(setting.offsetX));
  setint(ids.offsetX, setting.offsetX);

  register_handler_map(s_handlerMap, ids.offsetY, make_long_integer_box_handler(setting.offsetY));
  setint(ids.offsetY, setting.offsetY);

  register_handler_map(s_handlerMap, ids.selectMonitor.id,
                       make_menu_button_handler(ids.selectMonitor.id,
                                                [ids]() {
                                                  Log::debug(TEXT("selectMonitor received"));
                                                  return std::make_pair(0, create_monitors_menu(ids.selectMonitor.base));
                                                }));
}

template <typename Enum, std::size_t Num>
static void update_radio_button_lock_status(HWND hWnd, const RadioButtonMap<Enum, Num> &m, bool isLocked) {
  for ([[maybe_unused]] auto const &[tag, id] : m) {
    EnableWindow(GetDlgItem(hWnd, id), !isLocked);
  }
}

static void update_per_orientation_lock_status(HWND hWnd, const PerOrientationSettingID &ids, bool isLocked) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  auto set = [isLocked, get](auto id) { EnableWindow(get(id), !isLocked); };
  set(ids.monitorNumber);
  set(ids.isConsiderTaskbar.id);
  update_radio_button_lock_status(hWnd, ids.windowArea, isLocked);
  set(ids.size);
  update_radio_button_lock_status(hWnd, ids.axis, isLocked);
  update_radio_button_lock_status(hWnd, ids.origin, isLocked);
  set(ids.offsetX);
  set(ids.offsetY);
}

static void update_lock_status(HWND hWnd, bool isLocked) {
  update_per_orientation_lock_status(hWnd, verticalSettingID, isLocked);
  update_per_orientation_lock_status(hWnd, horizontalSettingID, isLocked);
}

static std::pair<Win32::Menu, Win32::BorrowedMenu> create_profile_menu(bool isLocked) {
  auto menu = Win32::load_menu(hInstance, MAKEINTRESOURCE(IDM_PROFILE));
  auto submenu = Win32::get_sub_menu(menu, 0);
  // IDC_LOCK のチェック状態を変更する
  auto mii = Win32::make_sized_pod<MENUITEMINFO>();
  mii.fMask |= MIIM_STATE;
  mii.fState |= isLocked ? MFS_CHECKED : 0;
  SetMenuItemInfo(submenu.get(), IDC_LOCK, false, &mii);
  return std::make_pair(std::move(menu), std::move(submenu));
}

static void init_main_controlls(HWND hWnd) {
  auto &setting = s_currentGlobalSetting;
  static constexpr auto m = make_bool_check_button_map(IDC_ENABLED);

  register_handler_map(s_handlerMap, IDC_ENABLED, make_check_button_handler(m, setting.isEnabled));
  set_check_button(hWnd, m, setting.isEnabled);

  init_per_orientation_settings(hWnd, verticalSettingID, setting.currentProfile.verticalSetting);
  init_per_orientation_settings(hWnd, horizontalSettingID, setting.currentProfile.horizontalSetting);

  register_handler_map(s_handlerMap, IDC_OPEN_PROFILE_MENU,
                       make_menu_button_handler(IDC_OPEN_PROFILE_MENU,
                                                []() {
                                                  Log::debug(TEXT("IDC_OPEN_PROFILE_MENU received"));
                                                  return create_profile_menu(s_currentGlobalSetting.currentProfile.isLocked);
                                                }));
  register_handler_map(s_handlerMap, IDC_LOCK,
                       [](HWND hWnd, UINT, WPARAM, LPARAM) {
                         Log::debug(TEXT("IDC_LOCK received"));
                         s_currentGlobalSetting.currentProfile.isLocked = !s_currentGlobalSetting.currentProfile.isLocked;
                         update_lock_status(hWnd, s_currentGlobalSetting.currentProfile.isLocked);
                         return HandlerResult{true, TRUE};
                       });
}

static void update_target_status_text(HWND hWnd, const TargetStatus &ts) {
  TCHAR tmp[256];
  bool isHorizontal = false, isVertical = false;
  _tcscpy(tmp, TEXT("<target not found>"));
  if (ts.hWnd) {
    auto cW = Win32::width(ts.clientRect);
    auto cH = Win32::height(ts.clientRect);
    auto wW = Win32::width(ts.windowRect);
    auto wH = Win32::height(ts.windowRect);
    isHorizontal = cW > cH;
    isVertical = !isHorizontal;
    _stprintf(tmp,
              TEXT("0x%08X (%ld,%ld) [%ldx%ld] / (%ld,%ld) [%ldx%ld] (%ls)"),
              static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(ts.hWnd)),
              ts.windowRect.left, ts.windowRect.top, wW, wH,
              ts.clientRect.left, ts.clientRect.top, cW, cH,
              Win32::load_string(hInstance, isHorizontal ? IDS_HORIZONTAL:IDS_VERTICAL).c_str());
  }
  SetWindowText(GetDlgItem(hWnd, IDC_TARGET_STATUS), tmp);
  s_verticalGroupBox.set_selected(isVertical);
  s_horizontalGroupBox.set_selected(isHorizontal);
}

static INT_PTR main_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    // override wndproc for group boxes.
    s_verticalGroupBox.override_window_proc(GetDlgItem(hWnd, IDC_V_GROUPBOX));
    s_horizontalGroupBox.override_window_proc(GetDlgItem(hWnd, IDC_H_GROUPBOX));
    // add "quit" to system menu, individual to "close".
    HMENU hMenu = GetSystemMenu(hWnd, FALSE);
    AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
    AppendMenu(hMenu, MF_ENABLED | MF_STRING, IDC_QUIT, Win32::load_string(hInstance, IDS_QUIT).c_str());
    // disable close button / menu
    EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
    //
    s_currentGlobalSetting = load_global_setting();
    init_main_controlls(hWnd);
    register_handler_map(s_handlerMap, IDC_HIDE,
                         [](HWND hWnd) {
                           Log::debug(TEXT("IDC_HIDE received"));
                           ShowWindow(hWnd, SW_HIDE);
                           return HandlerResult{true, TRUE};
                         });
    register_handler_map(s_handlerMap, IDC_QUIT,
                         [](HWND hWnd) {
                           Log::debug(TEXT("IDC_QUIT received"));
                           save_global_setting(s_currentGlobalSetting);
                           delete_tasktray_icon(hWnd);
                           KillTimer(hWnd, TIMER_ID);
                           DestroyWindow(hWnd);
                           return HandlerResult{true, TRUE};
                         });
    register_handler_map(s_handlerMap, IDC_SHOW,
                         [](HWND hWnd) {
                           Log::debug(TEXT("IDC_SHOW received"));
                           ShowWindow(hWnd, SW_SHOW);
                           SetForegroundWindow(hWnd);
                           return HandlerResult{true, TRUE};
                         });
    update_lock_status(hWnd, s_currentGlobalSetting.currentProfile.isLocked);
    add_tasktray_icon(hWnd, appIconSm.get());
    PostMessage(hWnd, WM_TIMER, TIMER_ID, 0);
    SetTimer(hWnd, TIMER_ID, TIMER_PERIOD, nullptr);
    update_monitors();
    if (auto r = adjust_target(hWnd, true); r.isChanged)
      update_target_status_text(hWnd, r.targetStatus);
    return TRUE;
  }

  case WM_DESTROY:
    s_verticalGroupBox.restore_window_proc();
    s_horizontalGroupBox.restore_window_proc();
    PostQuitMessage(0);
    return TRUE;

  case WM_TASKTRAY:
    switch (lParam) {
    case WM_RBUTTONDOWN:
      show_popup_menu(hWnd, TRUE);
      return TRUE;

    case WM_LBUTTONDOWN:
      // show / hide main dialog
      if (IsWindowVisible(hWnd))
        PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
      else
        PostMessage(hWnd, WM_COMMAND, IDC_SHOW, 0);
      return TRUE;
    }
    return FALSE;

  case WM_SYSCOMMAND:
    switch (LOWORD(wParam)) {
    case SC_MINIMIZE:
      PostMessage(hWnd, WM_COMMAND, IDC_HIDE, 0);
      return TRUE;

    case IDC_QUIT:
      PostMessage(hWnd, WM_COMMAND, IDC_QUIT, 0);
      return TRUE;
    }
    return FALSE;

  case WM_COMMAND: {
    UINT id = LOWORD(wParam);
    if (auto i = s_handlerMap.find(static_cast<int>(id)); i != s_handlerMap.end())
      if (auto ret = i->second(hWnd, msg, wParam, lParam); ret.first)
        return ret.second;
    if (id >= IDM_V_MONITOR_BASE && id < IDM_V_MONITOR_BASE + 2 + monitors.size()) {
      set_monitor_number(hWnd, IDC_V_MONITOR_NUMBER, id - IDM_V_MONITOR_BASE - 1);
      return TRUE;
    }
    if (id >= IDM_H_MONITOR_BASE && id < IDM_H_MONITOR_BASE + 2 + monitors.size()) {
      set_monitor_number(hWnd, IDC_H_MONITOR_NUMBER, id - IDM_H_MONITOR_BASE - 1);
      return TRUE;
    }
    return FALSE;
  }

  case WM_RBUTTONDOWN:
    show_popup_menu(hWnd);
    return TRUE;

  case WM_TIMER:
    if (auto r = adjust_target(hWnd, isDialogChanged); r.isChanged)
      update_target_status_text(hWnd, r.targetStatus);
    isDialogChanged = false;
    SetTimer(hWnd, TIMER_ID, TIMER_PERIOD, nullptr);
    return TRUE;

  case WM_DISPLAYCHANGE:
    update_monitors();
    return TRUE;

  case WM_SETFONT:
    // ダイアログのフォントを明示的に設定していると呼ばれる
    hFontMainDialog = reinterpret_cast<HFONT>(wParam);
    return TRUE;

  default:
    if (msg == msgTaskbarCreated) {
      add_tasktray_icon(hWnd, appIconSm.get());
      return TRUE;
    }
  }
  return FALSE;
}

static void register_main_dialog_class(HINSTANCE hInst) {
  auto wc = Win32::make_sized_pod<WNDCLASSEX>();
  wc.style = 0;
  wc.lpfnWndProc = reinterpret_cast<WNDPROC>(DefDlgProc);
  wc.cbClsExtra = 0;
  wc.cbWndExtra = DLGWINDOWEXTRA;
  wc.hInstance = hInst;
  wc.hIcon = appIcon.get();
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = TEXT(UMAPITA_MAIN_WINDOW_CLASS);
  wc.hIconSm = appIconSm.get();
  RegisterClassEx(&wc);
}

int WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
  hInstance = hInst;

  if (HWND hWnd = FindWindow(TEXT(UMAPITA_MAIN_WINDOW_CLASS), nullptr); hWnd) {
    PostMessage(hWnd, WM_COMMAND, IDC_SHOW, 0);
    return 0;
  }

  auto cx = GetSystemMetrics(SM_CXICON);
  auto cy = GetSystemMetrics(SM_CYICON);

  appIcon = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), cx, cy, 0);
  appIconSm = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), 16, 16, 0);

  register_main_dialog_class(hInst);
  msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));

  HWND hWnd = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_UMAPITA_MAIN), nullptr, &main_dialog_proc);
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (IsDialogMessage(hWnd, &msg))
      continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return msg.wParam;
}
