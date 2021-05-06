#include "pch.h"
#include "am/win32util.h"
#include "am/win32reg.h"
#include "am/win32reg_mapper.h"
#include "am/win32handler.h"
#include "am/win32custom_control.h"
#include "am/win32dialog.h"
#include "umapita_def.h"
#include "umapita_misc.h"
#include "umapita_setting.h"
#include "umapita_registry.h"
#include "umapita_monitors.h"
#include "umapita_custom_group_box.h"
#include "umapita_save_dialog_box.h"
#include "umapita_target_status.h"
#include "umapita_keyhook.h"
#include "umapita_res.h"

namespace Win32 = AM::Win32;
using AM::Log;
using Win32::Window;

//
// main dialog
//
namespace Handler = AM::Win32::Handler;
using CommandHandlerMap = Handler::Map<int, Handler::DialogMessageTraits>;

static CommandHandlerMap s_commandHandlerMap;
static UmapitaCustomGroupBox s_verticalGroupBox, s_horizontalGroupBox;
static UmapitaMonitors s_monitors;
static Umapita::TargetStatus s_lastTargetStatus;
static bool s_isDialogChanged = false;
static UINT s_msgTaskbarCreated = 0;
static Win32::Icon s_appIcon = nullptr, s_appIconSm = nullptr;
static UmapitaSetting::Global s_currentGlobalSetting{UmapitaSetting::DEFAULT_GLOBAL.clone<Win32::tstring>()};

//
// タスクトレイアイコン
//
static BOOL add_tasktray_icon(Window window, HICON hIcon) {
  auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
  nid.hWnd = window.get();
  nid.uID = TASKTRAY_ID;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TASKTRAY;
  nid.hIcon = hIcon;
  LoadString(window.get_instance(), IDS_TASKTRAY_TIP, nid.szTip, std::size(nid.szTip));
  return Shell_NotifyIcon(NIM_ADD, &nid);
}

static void delete_tasktray_icon(Window window) {
  auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
  nid.hWnd = window.get();
  nid.uID = TASKTRAY_ID;
  Shell_NotifyIcon(NIM_DELETE, &nid);
}


//
// ポップアップメニュー
//
static void show_popup_menu(Window window, TPMPARAMS *pTpmp = nullptr) {
  POINT point;

  GetCursorPos(&point);
  window.set_foreground();

  auto menu = Win32::load_menu(window.get_instance(), MAKEINTRESOURCE(IDM_POPUP));
  auto submenu = Win32::get_sub_menu(menu, 0);
  TrackPopupMenuEx(submenu.hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, point.x, point.y, window.get(), pTpmp);
}


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
  RadioButtonMap<UmapitaSetting::PerOrientation::WindowArea, 2> windowArea;
  int size;
  RadioButtonMap<UmapitaSetting::PerOrientation::SizeAxis, 2> axis;
  RadioButtonMap<UmapitaSetting::PerOrientation::Origin, 9> origin;
  int offsetX, offsetY;
  SelectMonitorMap selectMonitor;
};

constexpr PerOrientationSettingID VERTICAL_SETTING_ID = {
  // monitorNumber
  IDC_V_MONITOR_NUMBER,
  // isConsiderTaskbar
  make_bool_check_button_map(IDC_V_IS_CONSIDER_TASKBAR),
  // windowArea
  {{{UmapitaSetting::PerOrientation::Whole, IDC_V_WHOLE_AREA},
    {UmapitaSetting::PerOrientation::Client, IDC_V_CLIENT_AREA}}},
  // size
  IDC_V_SIZE,
  // axis
  {{{UmapitaSetting::PerOrientation::Width, IDC_V_AXIS_WIDTH},
    {UmapitaSetting::PerOrientation::Height, IDC_V_AXIS_HEIGHT}}},
  // origin
  {{{UmapitaSetting::PerOrientation::N, IDC_V_ORIGIN_N},
    {UmapitaSetting::PerOrientation::S, IDC_V_ORIGIN_S},
    {UmapitaSetting::PerOrientation::W, IDC_V_ORIGIN_W},
    {UmapitaSetting::PerOrientation::E, IDC_V_ORIGIN_E},
    {UmapitaSetting::PerOrientation::NW, IDC_V_ORIGIN_NW},
    {UmapitaSetting::PerOrientation::NE, IDC_V_ORIGIN_NE},
    {UmapitaSetting::PerOrientation::SW, IDC_V_ORIGIN_SW},
    {UmapitaSetting::PerOrientation::SE, IDC_V_ORIGIN_SE},
    {UmapitaSetting::PerOrientation::C, IDC_V_ORIGIN_C}}},
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
  {{{UmapitaSetting::PerOrientation::Whole, IDC_H_WHOLE_AREA},
    {UmapitaSetting::PerOrientation::Client, IDC_H_CLIENT_AREA}}},
  // size
  IDC_H_SIZE,
  // axis
  {{{UmapitaSetting::PerOrientation::Width, IDC_H_AXIS_WIDTH},
    {UmapitaSetting::PerOrientation::Height, IDC_H_AXIS_HEIGHT}}},
  // origin
  {{{UmapitaSetting::PerOrientation::N, IDC_H_ORIGIN_N},
    {UmapitaSetting::PerOrientation::S, IDC_H_ORIGIN_S},
    {UmapitaSetting::PerOrientation::W, IDC_H_ORIGIN_W},
    {UmapitaSetting::PerOrientation::E, IDC_H_ORIGIN_E},
    {UmapitaSetting::PerOrientation::NW, IDC_H_ORIGIN_NW},
    {UmapitaSetting::PerOrientation::NE, IDC_H_ORIGIN_NE},
    {UmapitaSetting::PerOrientation::SW, IDC_H_ORIGIN_SW},
    {UmapitaSetting::PerOrientation::SE, IDC_H_ORIGIN_SE},
    {UmapitaSetting::PerOrientation::C, IDC_H_ORIGIN_C}}},
  // offsetX
  IDC_H_OFFSET_X,
  // offsetY
  IDC_H_OFFSET_Y,
  // selectMonitor
  {IDC_H_SELECT_MONITORS, IDM_H_MONITOR_BASE},
};


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
  dialog.get_item(id).set_text(Win32::asprintf(TEXT("%d"), num));
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
               s_currentGlobalSetting.common.isCurrentProfileChanged = true;
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
                 s_currentGlobalSetting.common.isCurrentProfileChanged = true;
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
                 s_currentGlobalSetting.common.isCurrentProfileChanged = true;
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
             auto tpmp = Win32::make_sized_pod<TPMPARAMS>();
             auto rect = control.get_window_rect();
             tpmp.rcExclude = rect;
             TrackPopupMenuEx(Win32::MenuHandle{menu}.hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, rect.right, rect.top, control.get_parent().get(), &tpmp);
             return TRUE;
           }
           }
           return FALSE;
         };
}

static void update_per_orientation_settings(Window dialog, const PerOrientationSettingID &ids, UmapitaSetting::PerOrientation &setting) {
  auto get = [dialog](auto id) { return dialog.get_item(id); };
  auto setint = [get](auto id, int v) {
                  get(id).set_text(Win32::asprintf(TEXT("%d"), v));
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

static void init_per_orientation_settings(Window dialog, const PerOrientationSettingID &ids, UmapitaSetting::PerOrientation &setting) {
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
                                            [ids, &setting](Window) {
                                              Log::debug(TEXT("selectMonitor received"));
                                              auto menu = Win32::create_popup_menu();
                                              int id = ids.selectMonitor.base;
                                              s_monitors.enum_monitors(
                                                [&setting, &id, &menu](auto index, auto const &m) {
                                                  auto const &[name, whole, work] = m;
                                                  auto const &rc = setting.isConsiderTaskbar ? work : whole;
                                                  AppendMenu(menu.get(), MF_STRING, id++,
                                                             Win32::asprintf(TEXT("%2d: (%6ld,%6ld)-(%6ld,%6ld) %ls"),
                                                                             index, rc.left, rc.top, rc.right, rc.bottom, name.c_str()).c_str());
                                                });
                                              return std::make_pair(0 /*dummy*/, std::move(menu));
                                            }));
}

static void update_profile_text(Window dialog) {
  Win32::tstring buf;
  auto item = dialog.get_item(IDC_SELECT_PROFILE);
  auto hInst = dialog.get_instance();

  if (s_currentGlobalSetting.common.currentProfileName.empty())
    buf = Win32::load_string(hInst, IDS_NEW_PROFILE);
  else {
    buf = s_currentGlobalSetting.common.currentProfileName;
    ComboBox_SelectString(item.get(), -1, buf.c_str());
  }

  if (s_currentGlobalSetting.common.isCurrentProfileChanged)
    buf += Win32::load_string(hInst, IDS_CHANGED_MARK);

  item.set_text(buf);
}

static void update_profile(Window dialog) {
  auto item = dialog.get_item(IDC_SELECT_PROFILE);
  auto ps = UmapitaRegistry::enum_profile();

  Umapita::fill_string_list_to_combobox(item, ps);
  item.enable(!ps.empty());
  update_profile_text(dialog);
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
  // テキストがセレクトされるのがうっとうしいのでクリアする
  item.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
}


static void update_per_orientation_lock_status(Window dialog, const PerOrientationSettingID &ids, bool isLocked) {
  auto set = [dialog, isLocked](auto id) { dialog.get_item(id).enable(!isLocked); };
  auto set_radio = [dialog, isLocked](const auto &m) {
                     for ([[maybe_unused]] auto const &[tag, id] : m) {
                       dialog.get_item(id).enable(!isLocked);
                     }
                   };
  set(ids.monitorNumber);
  set(ids.isConsiderTaskbar.id);
  set_radio(ids.windowArea);
  set(ids.size);
  set_radio(ids.axis);
  set_radio(ids.origin);
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
  set_check_button(dialog, make_bool_check_button_map(IDC_ENABLED), setting.common.isEnabled);
  update_profile(dialog);
  update_per_orientation_settings(dialog, VERTICAL_SETTING_ID, setting.currentProfile.vertical);
  update_per_orientation_settings(dialog, HORIZONTAL_SETTING_ID, setting.currentProfile.horizontal);
  update_lock_status(dialog);
}

static int save_as(Window dialog) {
  auto &s = s_currentGlobalSetting;
  auto [ret, profileName] = UmapitaSaveDialogBox::open(dialog, UmapitaSaveDialogBox::Save, s.common.currentProfileName);
  if (ret == IDCANCEL) {
    Log::debug(TEXT("save as: canceled"));
    return IDCANCEL;
  }
  s.common.currentProfileName = profileName;
  UmapitaRegistry::save_setting(s.common.currentProfileName, s.currentProfile);
  s.common.isCurrentProfileChanged = false;
  return IDOK;
}

static int save(Window dialog) {
  auto &s = s_currentGlobalSetting;
  Log::debug(TEXT("IDC_SAVE received"));

  if (s.common.currentProfileName.empty())
    return save_as(dialog);

  if (!s.common.isCurrentProfileChanged) {
    Log::debug(TEXT("unnecessary to save"));
    return IDOK;
  }
  UmapitaRegistry::save_setting(s.common.currentProfileName, s.currentProfile);
  s.common.isCurrentProfileChanged = false;
  return IDOK;
}

static int confirm_save(Window dialog) {
  if (!s_currentGlobalSetting.common.isCurrentProfileChanged) {
    Log::debug(TEXT("unnecessary to save"));
    return IDOK;
  }
  dialog.show(SW_SHOW);
  auto hInst = dialog.get_instance();
  auto ret = Win32::open_message_box_in_center(dialog,
                                               Win32::load_string(hInst, IDS_CONFIRM_SAVE),
                                               Win32::load_string(hInst, IDS_CONFIRM),
                                               MB_YESNOCANCEL);
  switch (ret) {
  case IDYES:
    return save(dialog);
  case IDNO:
    return IDOK;
  }
  return IDCANCEL;
}

static void init_main_controlls(Window dialog) {
  auto &setting = s_currentGlobalSetting;
  {
    static constexpr auto m = make_bool_check_button_map(IDC_ENABLED);
    register_handler(s_commandHandlerMap, IDC_ENABLED, make_check_button_handler(m, setting.common.isEnabled, true));
  }

  {
    // IDC_SELECT_PROFILE のエディットボックス部分を編集不可にする
    // EnableWindow だとなぜかプルダウンメニューが出なくなってしまうのでダメ
    auto cbi = Win32::make_sized_pod<COMBOBOXINFO>();
    GetComboBoxInfo(dialog.get_item(IDC_SELECT_PROFILE).get(), &cbi);
    Edit_SetReadOnly(cbi.hwndItem, true);
  }

  init_per_orientation_settings(dialog, VERTICAL_SETTING_ID, setting.currentProfile.vertical);
  init_per_orientation_settings(dialog, HORIZONTAL_SETTING_ID, setting.currentProfile.horizontal);

  register_handler(s_commandHandlerMap, IDC_SELECT_PROFILE,
                   [](Window dialog, Window control, int, int notify) {
                     switch (notify) {
                     case CBN_SELCHANGE:
                       dialog.post(WM_CHANGE_PROFILE, 0, control.to<LPARAM>());
                       return TRUE;
                     case CBN_SETFOCUS:
                     case CBN_CLOSEUP:
                       // テキストがセレクトされるのがうっとうしいのでクリアする
                       control.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
                       return TRUE;
                     }
                     return FALSE;
                   });
  register_handler(
    s_commandHandlerMap, IDC_OPEN_PROFILE_MENU,
    make_menu_button_handler(
      IDC_OPEN_PROFILE_MENU,
      [](Window dialog) {
        Log::debug(TEXT("IDC_OPEN_PROFILE_MENU received"));
        auto menu = Win32::load_menu(dialog.get_instance(), MAKEINTRESOURCE(IDM_PROFILE));
        auto submenu = Win32::get_sub_menu(menu, 0);
        auto const &s = s_currentGlobalSetting;

        // IDC_LOCK のチェック状態を変更する
        auto mii = Win32::make_sized_pod<MENUITEMINFO>();
        mii.fMask = MIIM_STATE;
        mii.fState = s.currentProfile.isLocked ? MFS_CHECKED : 0;
        SetMenuItemInfo(submenu.hMenu, IDC_LOCK, false, &mii);

        // 無名プロファイルでは IDC_RENAME と IDC_DELETE を無効にする
        mii.fMask = MIIM_STATE;
        mii.fState = s.common.currentProfileName.empty() ? MFS_DISABLED : MFS_ENABLED;
        SetMenuItemInfo(submenu.hMenu, IDC_RENAME, false, &mii);
        SetMenuItemInfo(submenu.hMenu, IDC_DELETE, false, &mii);

        // 名前付きかつ無変更ならば IDC_SAVE を無効にする
        mii.fMask = MIIM_STATE;
        mii.fState = !s.common.currentProfileName.empty() && !s.common.isCurrentProfileChanged ? MFS_DISABLED : MFS_ENABLED;
        SetMenuItemInfo(submenu.hMenu, IDC_SAVE, false, &mii);

        // 無名かつ無変更ならば IDC_NEW を無効にする
        mii.fMask = MIIM_STATE;
        mii.fState = s.common.currentProfileName.empty() && !s.common.isCurrentProfileChanged ? MFS_DISABLED : MFS_ENABLED;
        SetMenuItemInfo(submenu.hMenu, IDC_NEW, false, &mii);

        return std::make_pair(std::move(menu), std::move(submenu));
      }));
  register_handler(s_commandHandlerMap, IDC_LOCK,
                   [](Window) {
                     Log::debug(TEXT("IDC_LOCK received"));
                     s_currentGlobalSetting.currentProfile.isLocked = !s_currentGlobalSetting.currentProfile.isLocked;
                     s_currentGlobalSetting.common.isCurrentProfileChanged = true;
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
                     auto [ret, newProfileName] = UmapitaSaveDialogBox::open(dialog, UmapitaSaveDialogBox::Rename, s.common.currentProfileName);
                     if (ret == IDCANCEL) {
                       Log::debug(TEXT("rename: canceled"));
                       return TRUE;
                     }
                     if (newProfileName == s.common.currentProfileName) {
                       Log::debug(TEXT("rename: not changed"));
                       return TRUE;
                     }
                     s.common.currentProfileName = UmapitaRegistry::rename_profile(s.common.currentProfileName, newProfileName);
                     update_profile(dialog);
                     return TRUE;
                   });
  register_handler(s_commandHandlerMap, IDC_DELETE,
                   [](Window dialog) {
                     auto &s = s_currentGlobalSetting;
                     if (s.common.currentProfileName.empty())
                       return TRUE;
                     auto hInst = dialog.get_instance();
                     auto ret = Win32::open_message_box_in_center(dialog,
                                                                  Win32::asprintf(Win32::load_string(hInst, IDS_CONFIRM_DELETE),
                                                                                  s.common.currentProfileName.c_str()),
                                                                  Win32::load_string(hInst, IDS_CONFIRM_DELETE_TITLE),
                                                                  MB_OKCANCEL);
                     switch (ret) {
                     case IDCANCEL:
                       return TRUE;
                     }
                     UmapitaRegistry::delete_profile(s.common.currentProfileName);
                     s.common.currentProfileName = TEXT("");
                     s_isDialogChanged = true;
                     update_main_controlls(dialog);
                     return TRUE;
                   });

  register_handler(s_commandHandlerMap, IDC_NEW,
                   [](Window dialog) {
                     auto &s = s_currentGlobalSetting;
                     if (s.common.currentProfileName.empty() && !s.common.isCurrentProfileChanged) {
                       Log::debug(TEXT("unnecessary to do"));
                       return TRUE;
                     }
                     auto type = s.common.currentProfileName.empty() ? MB_YESNO : MB_YESNOCANCEL;
                     auto hInst = dialog.get_instance();
                     auto ret = Win32::open_message_box_in_center(dialog,
                                                                  Win32::load_string(hInst, IDS_CONFIRM_INIT),
                                                                  Win32::load_string(hInst, IDS_CONFIRM_INIT_TITLE),
                                                                  type);
                     switch (ret) {
                     case IDCANCEL:
                       return TRUE;
                     case IDYES:
                       break;
                     case IDNO:
                       if (s.common.isCurrentProfileChanged) {
                         switch (confirm_save(dialog)) {
                         case IDCANCEL:
                           return TRUE;
                         }
                       }
                       s.currentProfile = UmapitaSetting::DEFAULT_PER_PROFILE;
                       s.common.isCurrentProfileChanged = false;
                       break;
                     }
                     s.common.currentProfileName = TEXT("");
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

static void update_target_status_text(Window dialog, const Umapita::TargetStatus &ts) {
  bool isHorizontal = false, isVertical = false;
  auto text = Win32::tstring{TEXT("<target not found>")};
  if (ts.window) {
    auto cW = Win32::width(ts.clientRect);
    auto cH = Win32::height(ts.clientRect);
    auto wW = Win32::width(ts.windowRect);
    auto wH = Win32::height(ts.windowRect);
    isHorizontal = cW > cH;
    isVertical = !isHorizontal;
    text = Win32::asprintf(TEXT("0x%08X (%ld,%ld) [%ldx%ld] / (%ld,%ld) [%ldx%ld] (%ls)"),
                           static_cast<unsigned>(ts.window.to<LPARAM>()),
                           ts.windowRect.left, ts.windowRect.top, wW, wH,
                           ts.clientRect.left, ts.clientRect.top, cW, cH,
                           Win32::load_string(dialog.get_instance(), isHorizontal ? IDS_HORIZONTAL:IDS_VERTICAL).c_str());
  }
  dialog.get_item(IDC_TARGET_STATUS).set_text(text);
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
    s_currentGlobalSetting = UmapitaRegistry::load_global_setting();
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
                       UmapitaRegistry::save_global_setting(s_currentGlobalSetting);
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
    s_monitors = UmapitaMonitors{};
    s_isDialogChanged = true;
    return TRUE;
  }

  case WM_DESTROY:
    s_verticalGroupBox.restore_window_proc();
    s_horizontalGroupBox.restore_window_proc();
    PostQuitMessage(0);
    return TRUE;

  case WM_TASKTRAY:
    switch (lParam) {
    case WM_RBUTTONDOWN: {
      TPMPARAMS tpmp = Win32::make_sized_pod<TPMPARAMS>(), *pTpmp = nullptr;
      if (auto shell = Window::find(TEXT("Shell_TrayWnd"), nullptr); shell) {
        auto rect = shell.get_window_rect();
        tpmp.rcExclude = rect;
        pTpmp = &tpmp;
      }
      show_popup_menu(dialog, pTpmp);
      return TRUE;
    }

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
    if (s_isDialogChanged) {
      update_lock_status(dialog);
      update_profile_text(dialog);
      s_lastTargetStatus = Umapita::TargetStatus{};
      s_isDialogChanged = false;
    }
    if (auto ts = Umapita::TargetStatus::get(TARGET_WINDOW_CLASS, TARGET_WINDOW_NAME); ts != s_lastTargetStatus) {
      s_lastTargetStatus = ts;
      if (s_currentGlobalSetting.common.isEnabled)
        s_lastTargetStatus.adjust(s_monitors, s_currentGlobalSetting.currentProfile);
      update_target_status_text(dialog, s_lastTargetStatus);
      // キーフックの調整
      if (KeyHook::is_available()) {
        bool isEn = KeyHook::is_enabled();
        if (!isEn && ts.window && s_currentGlobalSetting.common.isEnabled) {
          // 調整が有効なのにキーフックが無効な時はキーフックを有効にする
          auto ret = KeyHook::enable(dialog.get(), WM_KEYHOOK, ts.window.get_thread_process_id().first);
          Log::info(TEXT("enable_keyhook %hs"), ret ? "succeeded":"failed");
        } else if (isEn && (!s_currentGlobalSetting.common.isEnabled || !ts.window)) {
          // 調整が無効なのにキーフックが有効な時はキーフックを無効にする
          auto ret = KeyHook::disable();
          Log::info(TEXT("disable_keyhook %hs"), ret ? "succeeded":"failed");
        }
      }
    }
    dialog.set_timer(TIMER_ID, TIMER_PERIOD, nullptr);
    return TRUE;

  case WM_DISPLAYCHANGE:
    s_monitors = UmapitaMonitors{};
    return TRUE;

  case WM_SETFONT: {
    // ダイアログのフォントを明示的に設定していると呼ばれる
    auto hFont = reinterpret_cast<HFONT>(wParam);
    s_verticalGroupBox.set_font(hFont);
    s_horizontalGroupBox.set_font(hFont);
    return TRUE;
  }

  case WM_CHANGE_PROFILE: {
    auto control = Window::from(lParam);
    auto n = control.get_text();
    update_profile_text(dialog); // ユーザの選択で変更されたエディットボックスの内容を一旦戻す（この後で再設定される）
    switch (confirm_save(dialog)) {
    case IDOK: {
      Log::debug(TEXT("selected: %ls"), n.c_str());
      s_currentGlobalSetting.common.currentProfileName = n;
      s_currentGlobalSetting.currentProfile = UmapitaRegistry::load_setting(n);
      s_currentGlobalSetting.common.isCurrentProfileChanged = false;
      break;
    }
    case IDCANCEL:
      Log::debug(TEXT("canceled"));
      break;
    }
    s_isDialogChanged = true;
    update_main_controlls(dialog);
    // テキストがセレクトされるのがうっとうしいのでクリアする
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
