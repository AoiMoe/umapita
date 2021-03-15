#include "pch.h"
#include "am/win32util.h"
#include "umapita_res.h"

namespace Win32 = AM::Win32;

constexpr UINT WM_TASKTRAY = WM_USER+0x1000;
constexpr UINT TASKTRAY_ID = 1;
constexpr UINT TIMER_ID = 1;
constexpr UINT TIMER_PERIOD = 200;
constexpr const LPCTSTR TARGET_WINDOW_CLASS = TEXT("UnityWndClass");
constexpr const LPCTSTR TARGET_WINDOW_NAME = TEXT("umamusume");
constexpr int MIN_WIDTH = 100;
constexpr int MIN_HEIGHT = 100;

using Monitor = std::tuple<RECT, std::wstring, BOOL>;
using Monitors = std::vector<Monitor>;

HINSTANCE hInstance;
UINT msgTaskbarCreated = 0;
HICON hAppIcon = nullptr;
Monitors monitors;
bool isEnabled = true;
bool isChanged = false;
HMENU hMenuVMonitors = nullptr, hMenuHMonitors = nullptr;

struct TargetStatus {
  HWND hWnd = nullptr;
  RECT windowRect{0, 0, 0, 0}, clientRect{0, 0, 0, 0};
};

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

struct PerOrientationSetting {
  int monitorNumber = 0;
  enum WindowArea { Whole, Client } windowArea = Client;
  LONG size = 0;
  enum Axis { Width, Height } axis = Height;
  enum Origin { N, S, W, E, NW, NE, SW, SE, C } origin = N;
  LONG offsetX = 0, offsetY = 0;
};

struct PerOrientationSettingID {
  int monitorNumber;
  RadioButtonMap<PerOrientationSetting::WindowArea, 2> windowArea;
  int size;
  RadioButtonMap<PerOrientationSetting::Axis, 2> axis;
  RadioButtonMap<PerOrientationSetting::Origin, 9> origin;
  int offsetX, offsetY;
};

PerOrientationSetting verticalSetting, horizontalSetting;
constexpr PerOrientationSettingID verticalSettingID = {
  // monitorNumber
  IDC_V_MONITOR_NUMBER,
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
};
constexpr PerOrientationSettingID horizontalSettingID = {
  // monitorNumber
  IDC_H_MONITOR_NUMBER,
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
};

inline NOTIFYICONDATA make_notify_icon_data(HWND hWnd, UINT uID) {
  auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
  nid.hWnd = hWnd;
  nid.uID = uID;
  return nid;
}

static BOOL add_tasktray_icon(HWND hWnd, HICON hIcon) {
  auto nid = make_notify_icon_data(hWnd, TASKTRAY_ID);

  nid.uFlags = NIF_ICON|NIF_MESSAGE|NIF_TIP;
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
  HMENU hMenu, hTasktrayMenu;

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

    hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDM_POPUP));
    hTasktrayMenu = GetSubMenu(hMenu, 0);
    TrackPopupMenuEx(hTasktrayMenu,
                     TPM_LEFTALIGN|TPM_LEFTBUTTON, point.x, point.y,
                     hWnd, pTpmp);
    DestroyMenu(hMenu);
}

using FindWindowResult = std::tuple<HWND, LPCTSTR, LPCTSTR>;
static BOOL CALLBACK find_window_callback(HWND hWnd, LPARAM lParam) {
  auto &[rhWnd, cls, name] = *reinterpret_cast<FindWindowResult *>(lParam);
  TCHAR tmp[128];
  if ((!cls || (GetClassName(hWnd, tmp, std::size(tmp)) && !_tcscmp(tmp, cls))) &&
      (!name || (GetWindowText(hWnd, tmp, std::size(tmp)) && !_tcscmp(tmp, name)))) {
    rhWnd = hWnd;
    return FALSE;
  }
  return TRUE;
}

static HWND find_window(LPCTSTR cls, LPCTSTR name) {
  FindWindowResult ret{nullptr, cls, name};
  EnumWindows(&find_window_callback, reinterpret_cast<LPARAM>(&ret));
  return std::get<HWND>(ret);
}

static BOOL update_monitors_callback(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
  Monitors &ms = *reinterpret_cast<Monitors *>(lParam);
  auto mi = Win32::make_sized_pod<MONITORINFOEX>();
  GetMonitorInfo(hMonitor, &mi);
  ms.emplace_back(mi.rcMonitor, mi.szDevice, !!(mi.dwFlags & MONITORINFOF_PRIMARY));
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
    if (auto result = std::find_if(ret.begin(), ret.end(),
                                   [](auto const &m) { return std::get<BOOL>(m); });
        result == ret.end()) {
      // not found
      ret.emplace(ret.begin(), whole, TEXT("<primary>"), FALSE);
    } else {
      ret.emplace(ret.begin(), std::get<RECT>(*result), TEXT("<primary>"), FALSE);
    }
  }
  // -1: whole virtual desktop
  {
    ret.emplace(ret.begin(), whole, TEXT("<all monitors>"), FALSE);
  }
  monitors = std::move(ret);
}

static HMENU create_monitors_menu(int idbase) {
  HMENU hMenu = CreatePopupMenu();
  int id = idbase;
  int index = -1;

  for (auto const & [rect, name, isprimary] : monitors) {
    TCHAR tmp[1024];
    _stprintf(tmp, TEXT("%2d: (%6ld,%6ld)-(%6ld,%6ld) %ls"),
              index++, rect.left, rect.top, rect.right, rect.bottom, name.c_str());
    AppendMenu(hMenu, MF_STRING, id++, tmp);
  }

  return hMenu;
}

static void popup_monitors_menu(HWND hWnd, int id, HMENU &hMenu, int idbase) {
  if (hMenu) {
    DestroyMenu(hMenu);
    hMenu = nullptr;
  }
  hMenu = create_monitors_menu(idbase);
  auto tpmp = Win32::make_sized_pod<TPMPARAMS>();
  RECT rect{0, 0, 0, 0};
  GetWindowRect(GetDlgItem(hWnd, id), &rect);
  tpmp.rcExclude = rect;
  TrackPopupMenuEx(hMenu, TPM_LEFTALIGN|TPM_LEFTBUTTON, rect.right, rect.top, hWnd, &tpmp);
}

static TargetStatus update_target_information(HWND hWndDialog) {
  TargetStatus ret;
  HWND hWndTarget = find_window(TARGET_WINDOW_CLASS, TARGET_WINDOW_NAME);
  TCHAR tmp[256];

  _tcscpy(tmp, TEXT("<target not found>"));
  if (hWndTarget) {
    auto wi = Win32::make_sized_pod<WINDOWINFO>();
    if (GetWindowInfo(hWndTarget, &wi)) {
      TCHAR tmp2[100];
      LONG w = wi.rcClient.right - wi.rcClient.left;
      LONG h = wi.rcClient.bottom - wi.rcClient.top;
      LoadString(hInstance, w > h ? IDS_HORIZONTAL:IDS_VERTICAL, tmp2, std::size(tmp2));
      _stprintf(tmp,
                TEXT("id=0x%08X (%ld,%ld)-(%ld,%ld) / (%ld,%ld)-(%ld,%ld) (%ls)"),
                static_cast<unsigned>(reinterpret_cast<ULONG_PTR>(hWndTarget)),
                wi.rcWindow.left, wi.rcWindow.top, wi.rcWindow.right, wi.rcWindow.bottom,
                wi.rcClient.left, wi.rcClient.top, wi.rcClient.right, wi.rcClient.bottom,
                tmp2);
      ret.hWnd = hWndTarget;
      ret.windowRect = wi.rcWindow;
      ret.clientRect = wi.rcClient;
    }
  }
  SetWindowText(GetDlgItem(hWndDialog, IDC_TARGET_STATUS), tmp);
  return ret;
}

inline auto width(const RECT &r) -> auto { return r.right - r.left; }
inline auto height(const RECT &r) -> auto { return r.bottom - r.top; }

static void update_target(HWND hWnd) {
  TargetStatus ts = update_target_information(hWnd);

  if (isEnabled && ts.hWnd && IsWindowVisible(ts.hWnd)) {
    auto cW = width(ts.clientRect);
    auto cH = height(ts.clientRect);
    auto wW = width(ts.windowRect);
    auto wH = height(ts.windowRect);
    auto ncX = ts.windowRect.left - ts.clientRect.left;
    auto ncY = ts.windowRect.top - ts.clientRect.top;
    auto ncW = wW - cW;
    auto ncH = wH - cH;
    const PerOrientationSetting &s = cW > cH ? horizontalSetting:verticalSetting;
    auto mn = s.monitorNumber + 1;

    if (static_cast<size_t>(mn) >= monitors.size())
      return;

    auto const &mR = std::get<RECT>(monitors[mn]);
    auto mW = width(mR);
    auto mH = height(mR);

    LONG idealCW = 0, idealCH = 0;
    switch (s.axis) {
    case PerOrientationSetting::Width: {
      auto sz = s.size == 0 ? mW : s.size;
      idealCW = s.windowArea == PerOrientationSetting::Client ? sz : sz - ncW;
      idealCH = cH * idealCW / cW;
      break;
    }
    case PerOrientationSetting::Height: {
      auto sz = s.size == 0 ? mH : s.size;
      idealCH = s.windowArea == PerOrientationSetting::Client ? sz : sz - ncH;
      idealCW = cW * idealCH / cH;
      break;
    }
    }
    auto idealW = s.windowArea == PerOrientationSetting::Client ? idealCW : idealCW + ncW;
    auto idealH = s.windowArea == PerOrientationSetting::Client ? idealCH : idealCH + ncH;
    LONG idealX = 0, idealY = 0;
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
    if (s.windowArea == PerOrientationSetting::Client) {
      idealX += ncX;
      idealY += ncY;
      idealW += ncW;
      idealH += ncH;
    }
    if ((idealX != ts.windowRect.left || idealY != ts.windowRect.top ||
         idealW != wW || idealH != wH) &&
        idealW > MIN_WIDTH && idealH > MIN_HEIGHT) {
      if (!SetWindowPos(ts.hWnd, nullptr, idealX, idealY, idealW, idealH,
                        SWP_NOACTIVATE | SWP_NOZORDER)) {
        printf("failed: %lu\n", GetLastError());
      }
      printf("%p, x=%ld, y=%ld, w=%ld, h=%ld\n", ts.hWnd, idealX, idealY, idealW, idealH);
    }
  }
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

template <typename Enum, std::size_t Num>
static Enum get_radio_buttons(HWND hWnd, const RadioButtonMap<Enum, Num> &m) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  for (auto const &[tag, id] : m) {
    if (Button_GetCheck(get(id)) == BST_CHECKED)
      return tag;
  }
  return m[0].first;
}

template <typename Enum>
static void set_check_button(HWND hWnd, const CheckButtonMap<Enum> &m, Enum v) {
  Button_SetCheck(GetDlgItem(hWnd, m.id), m.checked == v ? BST_CHECKED : BST_UNCHECKED);
}

template <typename Enum>
static Enum get_check_button(HWND hWnd, const CheckButtonMap<Enum> &m) {
  return Button_GetCheck(GetDlgItem(hWnd, m.id)) == BST_CHECKED ? m.checked : m.unchecked;
}

static void set_monitor_number(HWND hWnd, int id, int num) {
  TCHAR buf[256];
  _stprintf(buf, TEXT("%d"), num);
  SetWindowText(GetDlgItem(hWnd, id), buf);
}

static void init_per_orientation_settings(HWND hWnd, const PerOrientationSettingID &ids, const PerOrientationSetting &setting) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  auto setint = [get](auto id, int v) {
                  TCHAR buf[256];
                  _stprintf(buf, TEXT("%d"), v);
                  SetWindowText(get(id), buf);
                };

  setint(ids.monitorNumber, setting.monitorNumber);
  set_radio_buttons(hWnd, ids.windowArea, setting.windowArea);
  setint(ids.size, setting.size);
  set_radio_buttons(hWnd, ids.axis, setting.axis);
  set_radio_buttons(hWnd, ids.origin, setting.origin);
  setint(ids.offsetX, setting.offsetX);
  setint(ids.offsetY, setting.offsetY);
}

static void get_per_orientation_settings(HWND hWnd, const PerOrientationSettingID &ids, PerOrientationSetting &setting) {
  auto get = [hWnd](auto id) { return GetDlgItem(hWnd, id); };
  auto getint = [get](auto id) {
                  TCHAR buf[256];
                  GetWindowText(get(id), buf, std::size(buf));
                  return _tcstol(buf, nullptr, 10);
                };
  setting.monitorNumber = getint(ids.monitorNumber);
  setting.windowArea = get_radio_buttons(hWnd, ids.windowArea);
  setting.size = getint(ids.size);
  setting.axis = get_radio_buttons(hWnd, ids.axis);
  setting.origin = get_radio_buttons(hWnd, ids.origin);
  setting.offsetX = getint(ids.offsetX);
  setting.offsetY = getint(ids.offsetY);
}

static void init_main_controlls(HWND hWnd) {
  set_check_button(hWnd, make_bool_check_button_map(IDC_ENABLED), isEnabled);
  init_per_orientation_settings(hWnd, verticalSettingID, verticalSetting);
  init_per_orientation_settings(hWnd, horizontalSettingID, horizontalSetting);
}

static void update_dialog_items(HWND hWnd) {
  isEnabled = get_check_button(hWnd, make_bool_check_button_map(IDC_ENABLED));
  get_per_orientation_settings(hWnd, verticalSettingID, verticalSetting);
  get_per_orientation_settings(hWnd, horizontalSettingID, horizontalSetting);
}

static INT_PTR main_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    // add "quit" to system menu, individual to "close".
    HMENU hMenu = GetSystemMenu(hWnd, FALSE);
    TCHAR tmp[128];
    LoadString(hInstance, IDS_QUIT, tmp, std::size(tmp));
    AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
    AppendMenu(hMenu, MF_ENABLED|MF_STRING, IDC_QUIT, tmp);
    // disable close button / menu
    EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
    //
    init_main_controlls(hWnd);
    add_tasktray_icon(hWnd, hAppIcon);
    PostMessage(hWnd, WM_TIMER, TIMER_ID, 0);
    SetTimer(hWnd, TIMER_ID, TIMER_PERIOD, nullptr);
    update_monitors();
    return TRUE;
  }

  case WM_DESTROY:
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
    if (id >= IDC_BEGIN && id <= IDC_END)
      isChanged = true;
    switch (id) {
    case IDC_HIDE:
      ShowWindow(hWnd, SW_HIDE);
      return TRUE;

    case IDC_QUIT:
      delete_tasktray_icon(hWnd);
      KillTimer(hWnd, TIMER_ID);
      DestroyWindow(hWnd);
      return TRUE;

    case IDC_SHOW:
      ShowWindow(hWnd, SW_SHOW);
      SetActiveWindow(hWnd);
      return TRUE;

    case IDC_V_SELECT_MONITORS:
      popup_monitors_menu(hWnd, IDC_V_SELECT_MONITORS, hMenuVMonitors, IDM_V_MONITOR_BASE);
      return TRUE;

    case IDC_H_SELECT_MONITORS:
      popup_monitors_menu(hWnd, IDC_H_SELECT_MONITORS, hMenuHMonitors, IDM_H_MONITOR_BASE);
      return TRUE;

    }
    if (id >= IDM_V_MONITOR_BASE && id < IDM_V_MONITOR_BASE + 2 + monitors.size()) {
      isChanged = true;
      set_monitor_number(hWnd, IDC_V_MONITOR_NUMBER, id - IDM_V_MONITOR_BASE - 1);
      return TRUE;
    }
    if (id >= IDM_H_MONITOR_BASE && id < IDM_H_MONITOR_BASE + 2 + monitors.size()) {
      isChanged = true;
      set_monitor_number(hWnd, IDC_H_MONITOR_NUMBER, id - IDM_H_MONITOR_BASE - 1);
      return TRUE;
    }
    return FALSE;
  }

  case WM_RBUTTONDOWN:
    show_popup_menu(hWnd);
    return TRUE;

  case WM_TIMER:
    if (isChanged) {
      update_dialog_items(hWnd);
      isChanged = false;
    }
    update_target(hWnd);
    SetTimer(hWnd, TIMER_ID, TIMER_PERIOD, nullptr);
    return TRUE;

  case WM_DISPLAYCHANGE:
    update_monitors();
    return TRUE;

  default:
    if (msg == msgTaskbarCreated) {
      add_tasktray_icon(hWnd, hAppIcon);
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
  wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = TEXT(UMAPITA_MAIN_WINDOW_CLASS);
  wc.hIconSm = LoadIcon(nullptr, IDI_WINLOGO);
  RegisterClassEx(&wc);
}

int WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
  hInstance = hInst;

  if (HWND hWnd = find_window(TEXT(UMAPITA_MAIN_WINDOW_CLASS), nullptr); hWnd) {
    PostMessage(hWnd, WM_COMMAND, IDC_SHOW, 0);
    return 0;
  }

  register_main_dialog_class(hInst);
  msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));
  hAppIcon = LoadIcon(nullptr, IDI_WINLOGO);

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
