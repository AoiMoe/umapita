#include "pch.h"
#include "am/win32util.h"
#include "am/win32reg.h"
#include "am/win32handler.h"
#include "umapita_res.h"
#include "umapita_keyhook.h"

namespace Win32 = AM::Win32;
using AM::Log;
using Win32::Window;

constexpr UINT WM_TASKTRAY = WM_USER+0x1000;
constexpr UINT WM_CHANGE_PROFILE = WM_USER+0x1001;
constexpr UINT WM_KEYHOOK = WM_USER+0x1002;
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

UINT s_msgTaskbarCreated = 0;
Win32::Icon s_appIcon = nullptr, s_appIconSm = nullptr;
Monitors s_monitors;
HFONT s_hFontMainDialog = nullptr;

//
// 監視対象ウィンドウの状態
//
struct TargetStatus {
  Window window;
  RECT windowRect{0, 0, 0, 0};
  RECT clientRect{0, 0, 0, 0};
};
inline bool operator == (const TargetStatus &lhs, const TargetStatus &rhs) {
  using namespace AM::Win32::Op;
  return lhs.window == rhs.window && (!lhs.window || (lhs.windowRect == rhs.windowRect && lhs.clientRect == rhs.clientRect));
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

template <typename StringType>
struct GlobalSettingT {
  bool isEnabled = true;
  bool isCurrentProfileChanged = false;
  StringType currentProfileName{TEXT("")};  // XXX: gcc10 の libstdc++ でも basic_string は constexpr 化されてない
  Setting currentProfile{DEFAULT_SETTING};
  template <typename T>
  GlobalSettingT<T> clone() const {
    return GlobalSettingT<T>{isEnabled, isCurrentProfileChanged, currentProfileName, currentProfile};
  }
};
using GlobalSetting = GlobalSettingT<Win32::tstring>;

constexpr GlobalSettingT<LPCTSTR> DEFAULT_GLOBAL_SETTING{};

GlobalSetting s_currentGlobalSetting{DEFAULT_GLOBAL_SETTING.clone<Win32::tstring>()};

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

constexpr PerOrientationSettingID VERTICAL_SETTING_ID = {
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
constexpr PerOrientationSettingID HORIZONTAL_SETTING_ID = {
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
  Window m_window;
  WNDPROC m_lpPrevWndFunc = nullptr;
  bool m_isSelected = false;
  //
  static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    auto self = window.get_user_data<CustomGroupBox *>();
    switch (msg) {
    case WM_PAINT:
      AM::try_or_void([self, window]() { self->on_paint(window); });
      return 0;
    case WM_GETDLGCODE:
      return DLGC_STATIC;
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    }
    return CallWindowProc(self->m_lpPrevWndFunc, hWnd, msg, wParam, lParam);
  }
  void on_paint(Window window) {
    auto p = window.begin_paint();

    // テキスト描画
    auto scopedSelect = Win32::scoped_select_font(p.hdc(), s_hFontMainDialog);

    TEXTMETRIC tm;
    GetTextMetrics(p.hdc(), &tm);

    auto text = window.get_text();
    int len = text.size();

    SIZE size;
    GetTextExtentPoint32(p.hdc(), text.c_str(), len, &size);

    {
      auto scopedBkMode = Win32::scoped_set_bk_mode(p.hdc(), TRANSPARENT);
      auto scopedTextColor = Win32::scoped_set_text_color(p.hdc(), GetSysColor(COLOR_WINDOWTEXT));
      TextOut(p.hdc(), tm.tmAveCharWidth*5/4, 0, text.c_str(), len);
    }

    // 枠描画
    auto rect = window.get_client_rect();
    if (len) {
      // テキスト部分を描画エリアから除外する
      auto r = Win32::create_rect_region(tm.tmAveCharWidth, 0, tm.tmAveCharWidth*3/2 + size.cx, size.cy);
      ExtSelectClipRgn(p.hdc(), r.get(), RGN_DIFF);
    }
    {
      // 選択状態のときは黒くて幅 2 のラインを、非選択状態のときは灰色で幅 1 のラインを描く
      auto r = Win32::create_rect_region(rect.left, rect.top+size.cy/2, rect.right, rect.bottom);
      auto hBrush = reinterpret_cast<HBRUSH>(GetStockObject(m_isSelected ? BLACK_BRUSH : LTGRAY_BRUSH));
      auto w = m_isSelected ? 2 : 1;
      FrameRgn(p.hdc(), r.get(), hBrush, w, w);
    }
    SelectClipRgn(p.hdc(), nullptr);
  }
  void redraw() {
    if (m_window) {
      auto parent = m_window.get_parent();
      auto rect = m_window.get_window_rect();
      MapWindowPoints(HWND_DESKTOP, parent.get(), reinterpret_cast<LPPOINT>(&rect), 2);
      parent.invalidate_rect(rect, true);
      parent.update_window();
    }
  }
public:
  CustomGroupBox() { }
  void override_window_proc(Window window) {
    m_window = window;
    m_lpPrevWndFunc = m_window.get_wndproc();
    m_window.set_wndproc(WndProc);
    m_window.set_user_data(this);
    redraw();
  }
  void restore_window_proc() {
    if (m_window) {
      m_window.set_wndproc(m_lpPrevWndFunc);
      m_window.set_user_data(LONG_PTR{0});
      m_window.reset();
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
  using DefaultValueType = typename Map::DefaultValueType;
  LPCTSTR name;
  Map typeMap;
  DefaultValueType defaultValue;
};

template <typename Enum, std::size_t Num>
struct RegEnumMap {
  using ValueType = Enum;
  using DefaultValueType = Enum;
  std::array<std::pair<LPCTSTR, Enum>, Num> enumMap;
};

struct RegBoolMap {
  using ValueType = bool;
  using DefaultValueType = bool;
};

struct RegLongMap {
  using ValueType = LONG;
  using DefaultValueType = LONG;
};

struct RegStringMap {
  using ValueType = Win32::tstring;
  using DefaultValueType = LPCTSTR;
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

constexpr PerOrientationSettingRegMap REGMAP_VERTICAL_SETTING = {
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

constexpr PerOrientationSettingRegMap REGMAP_HORIZONTAL_SETTING = {
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

constexpr RegMap<RegBoolMap> REGMAP_IS_LOCKED = {TEXT("isLocked"), RegBoolMap{}, DEFAULT_SETTING.isLocked};
constexpr RegMap<RegBoolMap> REGMAP_IS_ENABLED = {TEXT("isEnabled"), RegBoolMap{}, DEFAULT_GLOBAL_SETTING.isEnabled};
constexpr RegMap<RegBoolMap> REGMAP_IS_CURRENT_PROFILE_CHANGED = {TEXT("isCurrentProfileChanged"), RegBoolMap{}, DEFAULT_GLOBAL_SETTING.isCurrentProfileChanged};
constexpr RegMap<RegStringMap> REGMAP_CURRENT_PROFILE_NAME = {TEXT("currentProfileName"), RegStringMap{}, DEFAULT_GLOBAL_SETTING.currentProfileName};


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

inline Win32::tstring reg_get_(const Win32::Reg::Key &key, const RegMap<RegStringMap> &m) {
  auto str = Win32::Reg::query_sz(key, m.name);
  Log::debug(TEXT("reg_get: %S -> %S"), m.name, str.c_str());
  return str;
}

template <class T>
auto reg_get(const Win32::Reg::Key &key, const RegMap<T> &m) {
  try {
    return reg_get_(key, m);
  }
  catch (Win32::Reg::ErrorCode &ex) {
    if (ex.code == ERROR_FILE_NOT_FOUND) {
      Log::warning(TEXT("\"%S\" is not found - use default value"), m.name);
      return typename T::ValueType{m.defaultValue};
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

inline void reg_put_(const Win32::Reg::Key &key, const RegMap<RegStringMap> &m, const Win32::tstring &v) {
  Win32::Reg::set_sz(key, m.name, v.c_str());
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

inline Win32::tstring encode_profile_name(LPCTSTR src) {
  Win32::tstring ret;

  for (; *src; src++) {
    switch (*src) {
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
      ret += *src;
    }
  }
  return ret;
}

inline Win32::tstring decode_profile_name(LPCTSTR src) {
  Win32::tstring ret;

  for (; *src; src++) {
    if (src[0] == TEXT('%') && _istxdigit(src[1]) && _istxdigit(src[2])) {
      TCHAR buf[3] = { src[1], src[2], L'\0' };
      TCHAR *next;
      ret += static_cast<TCHAR>(_tcstol(buf, &next, 16));
      src += 2;
    } else
      ret += *src;
  }
  return ret;
}

inline Win32::tstring make_regpath(LPCTSTR profileName) {
  Win32::tstring tmp{REG_PROJECT_ROOT_PATH};

  if (profileName) {
    tmp += TEXT("\\");
    tmp += REG_PROFILES_SUBKEY;
    if (*profileName) {
      tmp += TEXT("\\");
      tmp += encode_profile_name(profileName);
    }
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
        reg_get(key, REGMAP_IS_LOCKED),
        reg_get_per_orientation_setting(key, REGMAP_VERTICAL_SETTING),
        reg_get_per_orientation_setting(key, REGMAP_HORIZONTAL_SETTING),
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
      reg_put(key, REGMAP_IS_LOCKED, s.isLocked);
      reg_put_per_orientation_setting(key, REGMAP_VERTICAL_SETTING, s.verticalSetting);
      reg_put_per_orientation_setting(key, REGMAP_HORIZONTAL_SETTING, s.horizontalSetting);
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
  auto isCurrentProfileChanged = DEFAULT_GLOBAL_SETTING.isCurrentProfileChanged;
  Win32::tstring currentProfileName = DEFAULT_GLOBAL_SETTING.currentProfileName;
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path.c_str(), 0, KEY_READ);
    try {
      isEnabled = reg_get(key, REGMAP_IS_ENABLED);
      isCurrentProfileChanged = reg_get(key, REGMAP_IS_CURRENT_PROFILE_CHANGED);
      currentProfileName = reg_get(key, REGMAP_CURRENT_PROFILE_NAME);
    }
    catch (RegGetFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
  return GlobalSetting{isEnabled, isCurrentProfileChanged, currentProfileName, load_setting(nullptr)};
}

static void save_global_setting(const GlobalSetting &s) {
  auto path = make_regpath(nullptr);

  try {
    [[maybe_unused]] auto [key, disp ] = Win32::Reg::create_key(REG_ROOT_KEY, path.c_str(), 0, KEY_WRITE);
    try {
      reg_put(key, REGMAP_IS_ENABLED, s.isEnabled);
      reg_put(key, REGMAP_IS_CURRENT_PROFILE_CHANGED, s.isCurrentProfileChanged);
      reg_put(key, REGMAP_CURRENT_PROFILE_NAME, s.currentProfileName);
    }
    catch (RegPutFailed &) {
    }
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot read registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }
  save_setting(nullptr, s.currentProfile);
}

static std::vector<Win32::tstring> enum_profile() {
  std::vector<Win32::tstring> ret;

  Win32::tstring path{REG_PROJECT_ROOT_PATH};
  path += TEXT("\\");
  path += REG_PROFILES_SUBKEY;
  try {
    [[maybe_unused]] auto [key, disp] = Win32::Reg::create_key(REG_ROOT_KEY, path.c_str(), 0, KEY_READ);
    Win32::Reg::enum_key(key, [&ret](Win32::tstring name) { ret.emplace_back(decode_profile_name(name.c_str())); });
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot enum registry \"%S\": %hs(reason=%d)"), path.c_str(), ex.what(), ex.code);
  }

  return ret;
}

// newName は存在していてはいけない。
static Win32::tstring rename_profile(LPCTSTR oldName, LPCTSTR newName) {
  auto path = make_regpath(TEXT(""));
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path.c_str(), 0, KEY_READ);
    Win32::Reg::rename_key(key, encode_profile_name(oldName).c_str(), encode_profile_name(newName).c_str());
    return newName;
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot rename \"%S\" to \"%S\": %hs(reason=%d)"), oldName, newName, ex.what(), ex.code);
    return TEXT("");
  }
}

static void delete_profile(LPCTSTR name) {
  auto path = make_regpath(TEXT(""));
  try {
    auto key = Win32::Reg::open_key(REG_ROOT_KEY, path.c_str(), 0, KEY_WRITE);
    Win32::Reg::delete_tree(key, encode_profile_name(name).c_str());
  }
  catch (Win32::Reg::ErrorCode &ex) {
    Log::debug(TEXT("cannot delete \"%S\": %hs(reason=%d, path=\"%S\")"), name, ex.what(), ex.code, path.c_str());
  }
}


//
// UI
//

inline NOTIFYICONDATA make_notify_icon_data(Window window, UINT uID) {
  auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
  nid.hWnd = window.get();
  nid.uID = uID;
  return nid;
}

static BOOL add_tasktray_icon(Window window, HICON hIcon) {
  auto nid = make_notify_icon_data(window, TASKTRAY_ID);

  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TASKTRAY;
  nid.hIcon = hIcon;
  LoadString(window.get_instance(), IDS_TASKTRAY_TIP, nid.szTip, std::size(nid.szTip));
  return Shell_NotifyIcon(NIM_ADD, &nid);
}

static void delete_tasktray_icon(Window window) {
  auto nid = make_notify_icon_data(window, TASKTRAY_ID);

  Shell_NotifyIcon(NIM_DELETE, &nid);
}

static void show_popup_menu(Window window, BOOL isTray = FALSE) {
  POINT point;
  TPMPARAMS tpmp = Win32::make_sized_pod<TPMPARAMS>(), *pTpmp = nullptr;

  if (isTray) {
    if (auto shell = Window::find(TEXT("Shell_TrayWnd"), nullptr); shell) {
      auto rect = shell.get_window_rect();
      tpmp.rcExclude = rect;
      pTpmp = &tpmp;
    }
  }

  GetCursorPos(&point);
  window.set_foreground();

  auto menu = Win32::load_menu(window.get_instance(), MAKEINTRESOURCE(IDM_POPUP));
  auto submenu = Win32::get_sub_menu(menu, 0);
  TrackPopupMenuEx(submenu.get(), TPM_LEFTALIGN | TPM_LEFTBUTTON, point.x, point.y, window.get(), pTpmp);
}

static CALLBACK BOOL update_monitors_callback(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
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
  s_monitors = std::move(ret);
}

static Win32::Menu create_monitors_menu(int idbase) {
  auto menu = Win32::create_popup_menu();
  int id = idbase;
  int index = -1;

  for ([[maybe_unused]] auto const &[name, whole, work, isPrimary] : s_monitors) {
    TCHAR tmp[1024];
    _stprintf(tmp, TEXT("%2d: (%6ld,%6ld)-(%6ld,%6ld) %ls"),
              index++, whole.left, whole.top, whole.right, whole.bottom, name.c_str());
    AppendMenu(menu.get(), MF_STRING, id++, tmp);
  }

  return menu;
}

template <typename MenuType>
void show_button_menu(Window window, const MenuType &menu) {
  auto tpmp = Win32::make_sized_pod<TPMPARAMS>();
  auto rect = window.get_window_rect();
  tpmp.rcExclude = rect;
  TrackPopupMenuEx(menu.get(), TPM_LEFTALIGN | TPM_LEFTBUTTON, rect.right, rect.top, window.get_parent().get(), &tpmp);
}

static const Monitor *get_current_monitor(int monitorNumber) {
  auto mn = monitorNumber + 1;

  if (mn < 0 || static_cast<size_t>(mn) >= s_monitors.size())
    return NULL;

  return &s_monitors[mn];
}

static TargetStatus get_target_information() {
  if (auto target = Window::find(TARGET_WINDOW_CLASS, TARGET_WINDOW_NAME); target) {
    try {
      auto wi = target.get_info();
      return {target, wi.rcWindow, wi.rcClient};
    }
    catch (Win32::Win32ErrorCode &) {
    }
  }
  return {};
}

struct AdjustTargetResult {
  bool isChanged;
  TargetStatus targetStatus;
};

static AdjustTargetResult adjust_target(Window dialog, bool isSettingChanged) {
  static TargetStatus lastTargetStatus;
  auto const &setting = s_currentGlobalSetting;
  auto const &profile = setting.currentProfile;

  // ターゲット情報の更新
  TargetStatus ts = get_target_information();

  // ターゲットも設定も変更されていない場合は終了
  if (!isSettingChanged && ts == lastTargetStatus)
    return {false, {}};

  if (KeyHook::is_available()) {
    bool isEn = KeyHook::is_enabled();
    if (!isEn && ts.window && setting.isEnabled) {
      // 調整が有効なのにキーフックが無効な時はキーフックを有効にする
      auto r = KeyHook::enable(dialog.get(), WM_KEYHOOK, ts.window.get_thread_process_id().first);
      Log::info(TEXT("enable_keyhook %hs"), r ? "succeeded":"failed");
    } else if (isEn && (!setting.isEnabled || !ts.window)) {
      // 調整が無効なのにキーフックが有効な時はキーフックを無効にする
      auto r = KeyHook::disable();
      Log::info(TEXT("disable_keyhook %hs"), r ? "succeeded":"failed");
    }
  }

  lastTargetStatus = ts;

  if (setting.isEnabled && ts.window && ts.window.is_visible()) {
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
    Log::debug(TEXT("%p, x=%ld, y=%ld, w=%ld, h=%ld"), ts.window.get(), idealX, idealY, idealW, idealH);
    if ((idealX != ts.windowRect.left || idealY != ts.windowRect.top || idealW != wW || idealH != wH) &&
        idealW > MIN_WIDTH && idealH > MIN_HEIGHT) {
      auto willingToUpdate = true;
      try {
        ts.window.set_pos(Window{}, idealX, idealY, idealW, idealH, SWP_NOACTIVATE | SWP_NOZORDER);
      }
      catch (Win32::Win32ErrorCode &ex) {
        // SetWindowPos に失敗
        Log::error(TEXT("SetWindowPos failed: %lu\n"), ex.code);
        if (ex.code == ERROR_ACCESS_DENIED) {
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
// メッセージボックスをいい感じに配置する
//
void adjust_popup(Window owner, Window target) {
  // メインウィンドウの真ん中に配置する
  auto rcOwner = owner.get_window_rect();
  auto rcTarget = target.get_window_rect();
  auto w = Win32::width(rcTarget);
  auto h = Win32::height(rcTarget);
  auto x = rcOwner.left + (Win32::width(rcOwner) - w)/2;
  auto y = rcOwner.top + (Win32::height(rcOwner) - h)/2;

  // 画面外にはみ出さないようにする
  auto hm = owner.get_monitor(MONITOR_DEFAULTTONULL);
  if (hm) {
    auto mi = Win32::make_sized_pod<MONITORINFOEX>();
    GetMonitorInfo(hm, &mi);
    x = std::max(x, mi.rcWork.left);
    y = std::max(y, mi.rcWork.top);
    auto r = std::min(x + w, mi.rcWork.right);
    auto b = std::min(y + h, mi.rcWork.bottom);
    x = r - w;
    y = b - h;
  }

  target.set_pos(Window{}, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int open_message_box(Window owner, LPCTSTR text, LPCTSTR caption, UINT type) {
  static Window s_owner = nullptr;
  static HHOOK s_hHook = nullptr;

  s_owner = owner;
  s_hHook = SetWindowsHookEx(WH_CBT,
                             [](int code, WPARAM wParam, LPARAM lParam) CALLBACK {
                               HHOOK hHook = s_hHook;
                               if (code == HCBT_ACTIVATE) {
                                 UnhookWindowsHookEx(hHook);
                                 s_hHook = nullptr;
                                 adjust_popup(s_owner, Window::from(wParam));
                               }
                               return CallNextHookEx(hHook, code, wParam, lParam);
                             },
                             owner.get_instance(), GetCurrentThreadId());
  auto ret = owner.message_box(text, caption, type);
  s_owner.reset();
  s_hHook = nullptr;
  return ret;
}

//
// 名前を付けて保存する or リネーム
//

static bool fill_profile_to_combobox(Window cb) {
  ComboBox_ResetContent(cb.get());

  auto ps = enum_profile();
  for (auto const &name : ps) {
    ComboBox_AddString(cb.get(), name.c_str());
  }

  return !ps.empty();
}

struct SaveDialogBox {
  enum Kind { Save, Rename };
private:
  Window m_owner;
  Kind m_kind;
  Win32::tstring m_profileName;
  SaveDialogBox(Window owner, Kind kind, LPCTSTR oldname) : m_owner{owner}, m_kind{kind}, m_profileName{oldname} { }
  //
  static CALLBACK INT_PTR s_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window window{hWnd};
    if (msg == WM_INITDIALOG) {
      window.set_dialog_user_data(lParam);
    }
    auto self = window.get_dialog_user_data<SaveDialogBox *>();
    return self ? self->dialog_proc(window, msg, wParam, lParam) : FALSE;
  }

  INT_PTR dialog_proc(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
      auto hInst = window.get_instance();
      fill_profile_to_combobox(window.get_item(IDC_SELECT_PROFILE));
      window.get_item(IDC_SELECT_PROFILE).set_text(m_profileName);
      window.get_item(IDOK).enable(false);
      window.set_text(Win32::load_string(hInst, Save ? IDS_SAVE_AS_TITLE : IDS_RENAME_TITLE));
      WCHAR buf[256];
      auto detail = Win32::load_string(hInst, m_kind == Save ? IDS_SAVE_AS_DETAIL : IDS_RENAME_DETAIL);
      _stprintf(buf, detail.c_str(), m_profileName.c_str());
      window.get_item(IDC_SAVE_DETAIL).set_text(buf);
      adjust_popup(m_owner, window);
      return TRUE;
    }

    case WM_COMMAND: {
      UINT id = LOWORD(wParam);

      switch (id) {
      case IDOK: {
        auto n = Win32::remove_ws_on_both_ends(window.get_item(IDC_SELECT_PROFILE).get_text());
        if (n.empty())
          return TRUE;
        if (auto ps = enum_profile(); std::find(ps.begin(), ps.end(), n) != ps.end()) {
          auto hInst = window.get_instance();
          TCHAR tmp[256];
          _stprintf(tmp, Win32::load_string(hInst, IDS_CONFIRM_OVERWRITE).c_str(), n.c_str());
          auto r = open_message_box(window, tmp, Win32::load_string(hInst, IDS_CONFIRM).c_str(), MB_OKCANCEL);
          if (r != IDOK)
            return TRUE;
        }
        m_profileName = std::move(n);
        window.end_dialog(IDOK);
        return TRUE;
      }

      case IDCANCEL:
        window.end_dialog(IDCANCEL);
        return TRUE;

      case IDC_SELECT_PROFILE:
        switch (HIWORD(wParam)) {
        case CBN_SELCHANGE:
          window.get_item(IDOK).enable(true);
          return TRUE;
        case CBN_EDITCHANGE: {
          auto n = Win32::remove_ws_on_both_ends(Window::from(lParam).get_text());
          window.get_item(IDOK).enable(!n.empty() && m_profileName != n);
          return TRUE;
        }
        default:
          return FALSE;
        }
      }
      return FALSE;
    }
    }

    return FALSE;
  }
  SaveDialogBox() { }
public:
  static std::pair<int, Win32::tstring> open(Window owner, Kind kind, LPCTSTR oldname) {
    SaveDialogBox sdb{owner, kind, oldname};
    auto r = DialogBoxParam(owner.get_instance(), MAKEINTRESOURCE(IDD_SAVE), owner.get(), &SaveDialogBox::s_dialog_proc, reinterpret_cast<LPARAM>(&sdb));
    switch (r) {
    case IDOK:
      return std::make_pair(IDOK, sdb.m_profileName);
    case IDCANCEL:
      return std::make_pair(IDCANCEL, Win32::tstring{});
    case -1:
      throw Win32::Win32ErrorCode{-1};
    case 0:
      throw AM::IllegalArgument{"invalid hWnd"};
    }
    throw AM::IllegalArgument{"unknown result of DialogBox"};
  }
};


//
// main dialog
//
namespace Handler = AM::Win32::Handler;
using CommandHandlerMap = Handler::Map<int, Handler::DialogMessageTraits>;

template <typename H>
void register_handler(CommandHandlerMap &hm, int id, H h) {
  hm.register_handler(id, h);
}

template <typename Enum, typename H>
void register_handler(CommandHandlerMap &hm, const CheckButtonMap<Enum> &m, H h) {
  hm.register_handler(m.id, h);
}

template <typename Enum, std::size_t Num, typename H>
void register_handler(CommandHandlerMap &hm, const RadioButtonMap<Enum, Num> &m, H h) {
  for (auto const &[tag, id] : m)
    hm.register_handler(id, h);
}

static CommandHandlerMap s_commandHandlerMap;
static bool s_isDialogChanged = false;

template <typename Enum, std::size_t Num>
static void set_radio_buttons(Window dialog, const RadioButtonMap<Enum, Num> &m, Enum v) {
  auto get = [dialog](auto id) { return dialog.get_item(id).get(); };
  for (auto const &[tag, id] : m)
    if (tag == v)
      Button_SetCheck(get(id), BST_CHECKED);
    else
      Button_SetCheck(get(id), BST_UNCHECKED);
}

template <typename Enum>
static void set_check_button(Window dialog, const CheckButtonMap<Enum> &m, Enum v) {
  Button_SetCheck(dialog.get_item(m.id).get(), m.checked == v ? BST_CHECKED : BST_UNCHECKED);
}

static void set_monitor_number(Window dialog, int id, int num) {
  TCHAR buf[256];
  _stprintf(buf, TEXT("%d"), num);
  dialog.get_item(id).set_text(buf);
}

static auto make_long_integer_box_handler(LONG &stor) {
  return [&stor](Window, Window control, int id, int notify) {
           switch (notify) {
           case EN_CHANGE: {
             auto text = control.get_text();
             auto val = _tcstol(text.c_str(), nullptr, 10);
             if (val != stor) {
               Log::debug(TEXT("text box %X changed: %d -> %d"), id, stor, val);
               stor = val;
               s_currentGlobalSetting.isCurrentProfileChanged = true;
               s_isDialogChanged = true;
             }
             return TRUE;
           }
           }
           return FALSE;
         };
}

template <typename Enum>
auto make_check_button_handler(const CheckButtonMap<Enum> &m, Enum &stor, bool isGlobal = false) {
  return [&m, &stor, isGlobal](Window, Window control, int id, int notify) {
           switch (notify) {
           case BN_CLICKED: {
             auto val = Button_GetCheck(control.get()) == BST_CHECKED ? m.checked : m.unchecked;
             if (val != stor) {
               Log::debug(TEXT("check box %X changed: %d -> %d"), id, static_cast<int>(stor) , static_cast<int>(val));
               stor = val;
               if (!isGlobal)
                 s_currentGlobalSetting.isCurrentProfileChanged = true;
               s_isDialogChanged = true;
             }
             return TRUE;
           }
           }
           return FALSE;
         };
}

template <typename Enum, std::size_t Num>
auto make_radio_button_map(const RadioButtonMap<Enum, Num> &m, Enum &stor) {
  return [&m, &stor](Window, Window control, int cid, int notify) {
           switch (notify) {
           case BN_CLICKED: {
             for (auto const &[tag, id] : m) {
               if (id == cid && tag != stor) {
                 Log::debug(TEXT("radio button %X changed: %d -> %d"), cid, static_cast<int>(stor), static_cast<int>(tag));
                 stor = tag;
                 s_currentGlobalSetting.isCurrentProfileChanged = true;
                 s_isDialogChanged = true;
                 return TRUE;
               }
             }
             Log::warning(TEXT("BN_CLICKED to the unknown radio button %X is received"), cid);
             return FALSE;
           }
           }
           return FALSE;
         };
}

template <typename MenuFactory>
auto make_menu_button_handler(int id, MenuFactory f) {
  return [id, f](Window dialog, Window control, int, int notify) {
           switch (notify) {
           case BN_CLICKED: {
             [[maybe_unused]] auto [housekeeper, menu] = f(dialog);
             show_button_menu(control, menu);
             return TRUE;
           }
           }
           return FALSE;
         };
}

static void update_per_orientation_settings(Window dialog, const PerOrientationSettingID &ids, PerOrientationSetting &setting) {
  auto get = [dialog](auto id) { return dialog.get_item(id); };
  auto setint = [get](auto id, int v) {
                  TCHAR buf[256];
                  _stprintf(buf, TEXT("%d"), v);
                  get(id).set_text(buf);
                };
  setint(ids.monitorNumber, setting.monitorNumber);
  set_check_button(dialog, ids.isConsiderTaskbar, setting.isConsiderTaskbar);
  set_radio_buttons(dialog, ids.windowArea, setting.windowArea);
  setint(ids.size, setting.size);
  set_radio_buttons(dialog, ids.axis, setting.axis);
  set_radio_buttons(dialog, ids.origin, setting.origin);
  setint(ids.offsetX, setting.offsetX);
  setint(ids.offsetY, setting.offsetY);
}

static void init_per_orientation_settings(Window dialog, const PerOrientationSettingID &ids, PerOrientationSetting &setting) {
  register_handler(s_commandHandlerMap, ids.monitorNumber, make_long_integer_box_handler(setting.monitorNumber));
  register_handler(s_commandHandlerMap, ids.isConsiderTaskbar, make_check_button_handler(ids.isConsiderTaskbar, setting.isConsiderTaskbar));
  register_handler(s_commandHandlerMap, ids.windowArea, make_radio_button_map(ids.windowArea, setting.windowArea));
  register_handler(s_commandHandlerMap, ids.size, make_long_integer_box_handler(setting.size));
  register_handler(s_commandHandlerMap, ids.axis, make_radio_button_map(ids.axis, setting.axis));
  register_handler(s_commandHandlerMap, ids.origin, make_radio_button_map(ids.origin, setting.origin));
  register_handler(s_commandHandlerMap, ids.offsetX, make_long_integer_box_handler(setting.offsetX));
  register_handler(s_commandHandlerMap, ids.offsetY, make_long_integer_box_handler(setting.offsetY));
  register_handler(s_commandHandlerMap, ids.selectMonitor.id,
                   make_menu_button_handler(ids.selectMonitor.id,
                                            [ids](Window) {
                                              Log::debug(TEXT("selectMonitor received"));
                                              return std::make_pair(0, create_monitors_menu(ids.selectMonitor.base));
                                            }));
}

static void set_profile_text(Window dialog) {
  Win32::tstring buf;
  auto item = dialog.get_item(IDC_SELECT_PROFILE);
  auto hInst = dialog.get_instance();

  if (s_currentGlobalSetting.currentProfileName.empty())
    buf = Win32::load_string(hInst, IDS_NEW_PROFILE);
  else {
    buf = s_currentGlobalSetting.currentProfileName;
    ComboBox_SelectString(item.get(), -1, buf.c_str());
  }

  if (s_currentGlobalSetting.isCurrentProfileChanged)
    buf += Win32::load_string(hInst, IDS_CHANGED_MARK);

  item.set_text(buf);
}

static void update_profile(Window dialog) {
  auto item = dialog.get_item(IDC_SELECT_PROFILE);
  auto isEnabled = fill_profile_to_combobox(item);

  item.enable(isEnabled);
  set_profile_text(dialog);
}

static int save_as(Window dialog) {
  auto &s = s_currentGlobalSetting;
  auto [ret, profileName] = SaveDialogBox::open(dialog, SaveDialogBox::Save, s.currentProfileName.c_str());
  if (ret == IDCANCEL) {
    Log::debug(TEXT("save as: canceled"));
    return IDCANCEL;
  }
  s.currentProfileName = profileName;
  save_setting(s.currentProfileName.c_str(), s.currentProfile);
  s.isCurrentProfileChanged = false;
  return IDOK;
}

static int save(Window dialog) {
  auto &s = s_currentGlobalSetting;
  Log::debug(TEXT("IDC_SAVE received"));

  if (s.currentProfileName.empty())
    return save_as(dialog);

  if (!s.isCurrentProfileChanged) {
    Log::debug(TEXT("unnecessary to save"));
    return IDOK;
  }
  save_setting(s.currentProfileName.c_str(), s.currentProfile);
  s.isCurrentProfileChanged = false;
  return IDOK;
}

static int confirm_save(Window dialog) {
  if (!s_currentGlobalSetting.isCurrentProfileChanged) {
    Log::debug(TEXT("unnecessary to save"));
    return IDOK;
  }
  dialog.show(SW_SHOW);
  auto hInst = dialog.get_instance();
  auto ret = open_message_box(dialog,
                              Win32::load_string(hInst, IDS_CONFIRM_SAVE).c_str(),
                              Win32::load_string(hInst, IDS_CONFIRM).c_str(),
                              MB_YESNOCANCEL);
  switch (ret) {
  case IDYES:
    return save(dialog);
  case IDNO:
    return IDOK;
  }
  return IDCANCEL;
}

static void select_profile(Window dialog, int n) {
  auto item = dialog.get_item(IDC_SELECT_PROFILE);
  auto len = ComboBox_GetLBTextLen(item.get(), n);
  if (len == CB_ERR) {
    Log::info(TEXT("profile %d is not valid"), static_cast<int>(n));
    return;
  }
  auto str = Win32::get_sz(len, [item, n](LPTSTR buf, std::size_t len) { ComboBox_GetLBText(item.get(), n, buf); });
  item.set_text(str);
  dialog.post(WM_CHANGE_PROFILE, 0, item.to<LPARAM>());
  //テキストがセレクトされるのがうっとうしいのでクリアする
  item.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
}


static void init_profile(Window dialog) {
  auto item = dialog.get_item(IDC_SELECT_PROFILE);

  // エディットボックス部分を編集不可にする
  // EnableWindow だとなぜかプルダウンメニューが出なくなってしまうのでダメ
  auto cbi = Win32::make_sized_pod<COMBOBOXINFO>();
  GetComboBoxInfo(item.get(), &cbi);
  Edit_SetReadOnly(cbi.hwndItem, true);

  register_handler(s_commandHandlerMap, IDC_SELECT_PROFILE,
                   [](Window dialog, Window control, int, int notify) {
                     switch (notify) {
                     case CBN_SELCHANGE:
                       dialog.post(WM_CHANGE_PROFILE, 0, control.to<LPARAM>());
                       return TRUE;
                     case CBN_SETFOCUS:
                     case CBN_CLOSEUP:
                       //テキストがセレクトされるのがうっとうしいのでクリアする
                       control.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
                       return TRUE;
                     }
                     return FALSE;
                   });
}

template <typename Enum, std::size_t Num>
static void update_radio_button_lock_status(Window dialog, const RadioButtonMap<Enum, Num> &m, bool isLocked) {
  for ([[maybe_unused]] auto const &[tag, id] : m) {
    dialog.get_item(id).enable(!isLocked);
  }
}

static void update_per_orientation_lock_status(Window dialog, const PerOrientationSettingID &ids, bool isLocked) {
  auto set = [dialog, isLocked](auto id) { dialog.get_item(id).enable(!isLocked); };
  set(ids.monitorNumber);
  set(ids.isConsiderTaskbar.id);
  update_radio_button_lock_status(dialog, ids.windowArea, isLocked);
  set(ids.size);
  update_radio_button_lock_status(dialog, ids.axis, isLocked);
  update_radio_button_lock_status(dialog, ids.origin, isLocked);
  set(ids.offsetX);
  set(ids.offsetY);
}

static void update_lock_status(Window dialog) {
  auto isLocked = s_currentGlobalSetting.currentProfile.isLocked;
  update_per_orientation_lock_status(dialog, VERTICAL_SETTING_ID, isLocked);
  update_per_orientation_lock_status(dialog, HORIZONTAL_SETTING_ID, isLocked);
}

static void update_main_controlls(Window dialog) {
  auto &setting = s_currentGlobalSetting;
  set_check_button(dialog, make_bool_check_button_map(IDC_ENABLED), setting.isEnabled);
  update_profile(dialog);
  update_per_orientation_settings(dialog, VERTICAL_SETTING_ID, setting.currentProfile.verticalSetting);
  update_per_orientation_settings(dialog, HORIZONTAL_SETTING_ID, setting.currentProfile.horizontalSetting);
  update_lock_status(dialog);
}

static std::pair<Win32::Menu, Win32::BorrowedMenu> create_profile_menu(Window dialog) {
  auto menu = Win32::load_menu(dialog.get_instance(), MAKEINTRESOURCE(IDM_PROFILE));
  auto submenu = Win32::get_sub_menu(menu, 0);
  auto const &s = s_currentGlobalSetting;

  // IDC_LOCK のチェック状態を変更する
  auto mii = Win32::make_sized_pod<MENUITEMINFO>();
  mii.fMask = MIIM_STATE;
  mii.fState = s.currentProfile.isLocked ? MFS_CHECKED : 0;
  SetMenuItemInfo(submenu.get(), IDC_LOCK, false, &mii);

  // 無名プロファイルでは IDC_RENAME と IDC_DELETE を無効にする
  mii.fMask = MIIM_STATE;
  mii.fState = s.currentProfileName.empty() ? MFS_DISABLED : MFS_ENABLED;
  SetMenuItemInfo(submenu.get(), IDC_RENAME, false, &mii);
  SetMenuItemInfo(submenu.get(), IDC_DELETE, false, &mii);

  // 名前付きかつ無変更ならば IDC_SAVE を無効にする
  mii.fMask = MIIM_STATE;
  mii.fState = !s.currentProfileName.empty() && !s.isCurrentProfileChanged ? MFS_DISABLED : MFS_ENABLED;
  SetMenuItemInfo(submenu.get(), IDC_SAVE, false, &mii);

  // 無名かつ無変更ならば IDC_NEW を無効にする
  mii.fMask = MIIM_STATE;
  mii.fState = s.currentProfileName.empty() && !s.isCurrentProfileChanged ? MFS_DISABLED : MFS_ENABLED;
  SetMenuItemInfo(submenu.get(), IDC_NEW, false, &mii);

  return std::make_pair(std::move(menu), std::move(submenu));
}

static void init_main_controlls(Window dialog) {
  auto &setting = s_currentGlobalSetting;
  {
    static constexpr auto m = make_bool_check_button_map(IDC_ENABLED);
    register_handler(s_commandHandlerMap, IDC_ENABLED, make_check_button_handler(m, setting.isEnabled, true));
  }

  init_profile(dialog);
  init_per_orientation_settings(dialog, VERTICAL_SETTING_ID, setting.currentProfile.verticalSetting);
  init_per_orientation_settings(dialog, HORIZONTAL_SETTING_ID, setting.currentProfile.horizontalSetting);

  register_handler(s_commandHandlerMap, IDC_OPEN_PROFILE_MENU,
                   make_menu_button_handler(IDC_OPEN_PROFILE_MENU,
                                            [](Window dialog) {
                                              Log::debug(TEXT("IDC_OPEN_PROFILE_MENU received"));
                                              return create_profile_menu(dialog);
                                            }));
  register_handler(s_commandHandlerMap, IDC_LOCK,
                   [](Window) {
                     Log::debug(TEXT("IDC_LOCK received"));
                     s_currentGlobalSetting.currentProfile.isLocked = !s_currentGlobalSetting.currentProfile.isLocked;
                     s_currentGlobalSetting.isCurrentProfileChanged = true;
                     s_isDialogChanged = true;
                     return TRUE;
                   });
  register_handler(s_commandHandlerMap, IDC_SAVE,
                   [](Window dialog) {
                     save(dialog);
                     update_profile(dialog);
                     return TRUE;
                   });
  register_handler(s_commandHandlerMap, IDC_SAVE_AS,
                   [](Window dialog) {
                     save_as(dialog);
                     update_profile(dialog);
                     return TRUE;
                   });
  register_handler(s_commandHandlerMap, IDC_RENAME,
                   [](Window dialog) {
                     auto &s = s_currentGlobalSetting;
                     auto [ret, newProfileName] = SaveDialogBox::open(dialog, SaveDialogBox::Rename, s.currentProfileName.c_str());
                     if (ret == IDCANCEL) {
                       Log::debug(TEXT("rename: canceled"));
                       return TRUE;
                     }
                     if (newProfileName == s.currentProfileName) {
                       Log::debug(TEXT("rename: not changed"));
                       return TRUE;
                     }
                     delete_profile(newProfileName.c_str());
                     rename_profile(s.currentProfileName.c_str(), newProfileName.c_str());
                     s.currentProfileName = newProfileName;
                     update_profile(dialog);
                     return TRUE;
                   });
  register_handler(s_commandHandlerMap, IDC_DELETE,
                   [](Window dialog) {
                     auto &s = s_currentGlobalSetting;
                     if (s.currentProfileName.empty())
                       return TRUE;
                     TCHAR tmp[256];
                     auto hInst = dialog.get_instance();
                     _stprintf(tmp, Win32::load_string(hInst, IDS_CONFIRM_DELETE).c_str(), s.currentProfileName.c_str());
                     auto ret = open_message_box(dialog,
                                                 tmp,
                                                 Win32::load_string(hInst, IDS_CONFIRM_DELETE_TITLE).c_str(),
                                                 MB_OKCANCEL);
                     switch (ret) {
                     case IDCANCEL:
                       return TRUE;
                     }
                     delete_profile(s.currentProfileName.c_str());
                     s.currentProfileName = TEXT("");
                     s_isDialogChanged = true;
                     update_main_controlls(dialog);
                     return TRUE;
                   });

  register_handler(s_commandHandlerMap, IDC_NEW,
                   [](Window dialog) {
                     auto &s = s_currentGlobalSetting;
                     if (s.currentProfileName.empty() && !s.isCurrentProfileChanged) {
                       Log::debug(TEXT("unnecessary to do"));
                       return TRUE;
                     }
                     auto type = s.currentProfileName.empty() ? MB_YESNO : MB_YESNOCANCEL;
                     auto hInst = dialog.get_instance();
                     auto ret = open_message_box(dialog,
                                                 Win32::load_string(hInst, IDS_CONFIRM_INIT).c_str(),
                                                 Win32::load_string(hInst, IDS_CONFIRM_INIT_TITLE).c_str(),
                                                 type);
                     switch (ret) {
                     case IDCANCEL:
                       return TRUE;
                     case IDYES:
                       break;
                     case IDNO:
                       if (s.isCurrentProfileChanged) {
                         switch (confirm_save(dialog)) {
                         case IDCANCEL:
                           return TRUE;
                         }
                       }
                       s.currentProfile = DEFAULT_SETTING;
                       s.isCurrentProfileChanged = false;
                       break;
                     }
                     s.currentProfileName = TEXT("");
                     s_isDialogChanged = true;
                     update_main_controlls(dialog);
                     return TRUE;
                   });
  for (int id=IDC_SEL_BEGIN; id<=IDC_SEL_END; id++)
    register_handler(s_commandHandlerMap, id,
                     [](Window dialog, Window /*control*/, int id, int /*notify*/) {
                       select_profile(dialog, id-IDC_SEL_BEGIN);
                       return TRUE;
                     });

  update_main_controlls(dialog);
}

static void update_target_status_text(Window dialog, const TargetStatus &ts) {
  TCHAR tmp[256];
  bool isHorizontal = false, isVertical = false;
  _tcscpy(tmp, TEXT("<target not found>"));
  if (ts.window) {
    auto cW = Win32::width(ts.clientRect);
    auto cH = Win32::height(ts.clientRect);
    auto wW = Win32::width(ts.windowRect);
    auto wH = Win32::height(ts.windowRect);
    isHorizontal = cW > cH;
    isVertical = !isHorizontal;
    _stprintf(tmp,
              TEXT("0x%08X (%ld,%ld) [%ldx%ld] / (%ld,%ld) [%ldx%ld] (%ls)"),
              static_cast<unsigned>(ts.window.to<LPARAM>()),
              ts.windowRect.left, ts.windowRect.top, wW, wH,
              ts.clientRect.left, ts.clientRect.top, cW, cH,
              Win32::load_string(dialog.get_instance(), isHorizontal ? IDS_HORIZONTAL:IDS_VERTICAL).c_str());
  }
  dialog.get_item(IDC_TARGET_STATUS).set_text(tmp);
  s_verticalGroupBox.set_selected(isVertical);
  s_horizontalGroupBox.set_selected(isHorizontal);
}

static CALLBACK INT_PTR main_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  Window dialog{hWnd};

  // 再入カウンタ - モーダルダイアログが開いているかどうかを検知するために用意している。
  // モーダルダイアログが開いている時にメッセージを処理しようとすると 1 よりも大きくなる。
  // ただし、それ以外にも 1 より大きくなるケースがあるため、使い方に注意する必要がある。
  // eg. コントロールを変更すると親のウィンドウに WM_COMMAND などを SendMessage してくることがある。
  static int enterCount = 0;
  enterCount++;
  auto deleter = [](int *rcount) { (*rcount)--; };
  std::unique_ptr<int, decltype (deleter)> auto_decr{&enterCount, deleter};

  switch (msg) {
  case WM_INITDIALOG: {
    // override wndproc for group boxes.
    s_verticalGroupBox.override_window_proc(dialog.get_item(IDC_V_GROUPBOX));
    s_horizontalGroupBox.override_window_proc(dialog.get_item(IDC_H_GROUPBOX));
    // add "quit" to system menu, individual to "close".
    HMENU hMenu = dialog.get_system_menu();
    AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
    AppendMenu(hMenu, MF_ENABLED | MF_STRING, IDC_QUIT, Win32::load_string(dialog.get_instance(), IDS_QUIT).c_str());
    // disable close button / menu
    EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
    //
    s_currentGlobalSetting = load_global_setting();
    init_main_controlls(dialog);
    register_handler(s_commandHandlerMap, IDC_HIDE,
                     [](Window dialog) {
                       Log::debug(TEXT("IDC_HIDE received"));
                       dialog.show(SW_HIDE);
                       return TRUE;
                     });
    register_handler(s_commandHandlerMap, IDC_QUIT,
                     [](Window dialog) {
                       Log::debug(TEXT("IDC_QUIT received"));
                       save_global_setting(s_currentGlobalSetting);
                       delete_tasktray_icon(dialog);
                       dialog.kill_timer(TIMER_ID);
                       dialog.destroy();
                       return TRUE;
                     });
    register_handler(s_commandHandlerMap, IDC_SHOW,
                     [](Window dialog) {
                       Log::debug(TEXT("IDC_SHOW received"));
                       dialog.show(SW_SHOW);
                       dialog.set_foreground();
                       return TRUE;
                     });
    for (auto i=0; i<MAX_AVAILABLE_MONITORS+2; i++) {
      register_handler(s_commandHandlerMap, IDM_V_MONITOR_BASE+i,
                       [](Window dialog, Window, int id, int) {
                         set_monitor_number(dialog, IDC_V_MONITOR_NUMBER, id - IDM_V_MONITOR_BASE - 1);
                         return TRUE;
                       });
      register_handler(s_commandHandlerMap, IDM_H_MONITOR_BASE+i,
                       [](Window dialog, Window, int id, int) {
                         set_monitor_number(dialog, IDC_H_MONITOR_NUMBER, id - IDM_H_MONITOR_BASE - 1);
                         return TRUE;
                       });
    }
    add_tasktray_icon(dialog, s_appIconSm.get());
    dialog.post(WM_TIMER, TIMER_ID, 0);
    dialog.set_timer(TIMER_ID, TIMER_PERIOD, nullptr);
    update_monitors();
    if (auto r = adjust_target(dialog, true); r.isChanged)
      update_target_status_text(dialog, r.targetStatus);
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
      show_popup_menu(dialog, TRUE);
      return TRUE;

    case WM_LBUTTONDOWN:
      // show / hide main dialog
      if (dialog.is_visible())
        dialog.post(WM_SYSCOMMAND, SC_MINIMIZE, 0);
      else
        dialog.post(WM_COMMAND, IDC_SHOW, 0);
      return TRUE;
    }
    return FALSE;

  case WM_SYSCOMMAND:
    switch (LOWORD(wParam)) {
    case SC_MINIMIZE:
      dialog.post(WM_COMMAND, IDC_HIDE, 0);
      return TRUE;

    case IDC_QUIT:
      dialog.post(WM_COMMAND, IDC_QUIT, 0);
      return TRUE;
    }
    return FALSE;

  case WM_COMMAND:
    return s_commandHandlerMap.invoke(LOWORD(wParam), dialog, msg, wParam, lParam);

  case WM_RBUTTONDOWN:
    show_popup_menu(dialog);
    return TRUE;

  case WM_TIMER:
    if (auto r = adjust_target(dialog, s_isDialogChanged); r.isChanged) {
      update_target_status_text(dialog, r.targetStatus);
      update_lock_status(dialog);
      set_profile_text(dialog);
    }
    s_isDialogChanged = false;
    dialog.set_timer(TIMER_ID, TIMER_PERIOD, nullptr);
    return TRUE;

  case WM_DISPLAYCHANGE:
    update_monitors();
    return TRUE;

  case WM_SETFONT:
    // ダイアログのフォントを明示的に設定していると呼ばれる
    s_hFontMainDialog = reinterpret_cast<HFONT>(wParam);
    return TRUE;

  case WM_CHANGE_PROFILE: {
    auto control = Window::from(lParam);
    auto n = control.get_text();
    set_profile_text(dialog); // ユーザの選択で変更定されたエディットボックスの内容を一旦戻す（後に再設定される）
    switch (confirm_save(dialog)) {
    case IDOK: {
      Log::debug(TEXT("selected: %S"), n.c_str());
      s_currentGlobalSetting.currentProfileName = n;
      s_currentGlobalSetting.currentProfile = load_setting(n.c_str());
      s_currentGlobalSetting.isCurrentProfileChanged = false;
      break;
    }
    case IDCANCEL:
      Log::debug(TEXT("canceled"));
      break;
    }
    s_isDialogChanged = true;
    update_main_controlls(dialog);
    //テキストがセレクトされるのがうっとうしいのでクリアする
    control.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
    return TRUE;
  }

  case WM_KEYHOOK: {
    Log::debug(TEXT("WM_KEYHOOK: wParam=%X, lParam=%X, enterCount=%d"),
               static_cast<unsigned>(wParam), static_cast<unsigned>(lParam), enterCount);
    if (lParam & 0x80000000U) {
      // keydown
      if ((lParam & 0x20000000U) && wParam >= 0x30 && wParam <= 0x39) {
        // ALT+数字
        auto n = static_cast<WORD>(wParam & 0x0F);
        n = (n ? n : 10) - 1;
        if (enterCount == 1) {
          // enterCount が 1 よりも大きい場合、モーダルダイアログが開いている可能性があるので送らない。
          // モーダルダイアログが開いているときに送ると、別のモーダルダイアログが開いたり、いろいろ嫌なことが起こる。
          dialog.post(WM_COMMAND, MAKEWPARAM(n + IDC_SEL_BEGIN, 0), 0);
        }
      }
    }
    return TRUE;
  }

  default:
    if (msg == s_msgTaskbarCreated) {
      add_tasktray_icon(dialog, s_appIconSm.get());
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
  wc.hIcon = s_appIcon.get();
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = TEXT(UMAPITA_MAIN_WINDOW_CLASS);
  wc.hIconSm = s_appIconSm.get();
  RegisterClassEx(&wc);
}

int WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
  if (auto w = Window::find(TEXT(UMAPITA_MAIN_WINDOW_CLASS), nullptr); w) {
    w.post(WM_COMMAND, IDC_SHOW, 0);
    return 0;
  }

  KeyHook::load();

  auto cx = GetSystemMetrics(SM_CXICON);
  auto cy = GetSystemMetrics(SM_CYICON);

  s_appIcon = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), cx, cy, 0);
  s_appIconSm = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), 16, 16, 0);

  register_main_dialog_class(hInst);
  s_msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));

  Window window{CreateDialog(hInst, MAKEINTRESOURCE(IDD_UMAPITA_MAIN), nullptr, &main_dialog_proc)};
  auto hAccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDA_UMAPITA));
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (window.translate_accelerator(hAccel, &msg))
      continue;
    if (window.is_dialog_message(&msg))
      continue;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return msg.wParam;
}
