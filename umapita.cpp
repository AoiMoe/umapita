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

template <typename Enum>
struct CheckButtonMap {
  Enum unchecked;
  Enum checked;
  int id;
};

constexpr static CheckButtonMap<bool> make_bool_check_button_map(int id) {
  return CheckButtonMap<bool>{false, true, id};
}

template <typename Enum, std::size_t Num>
using RadioButtonMap = std::array<std::pair<Enum, int>, Num>;

//
// main dialog
//
class MainDialogBox : public Win32::Dialog::Template<MainDialogBox> {
  friend class Win32::Dialog::Template<MainDialogBox>;
private:
  static UINT s_msgTaskbarCreated;
  static Win32::Icon s_appIcon, s_appIconSm;
  //
  UmapitaCustomGroupBox m_verticalGroupBox, m_horizontalGroupBox;
  UmapitaMonitors m_monitors;
  Umapita::TargetStatus m_lastTargetStatus;
  bool m_isDialogChanged = false;
  UmapitaSetting::Global m_currentGlobalSetting{UmapitaSetting::DEFAULT_GLOBAL.clone<Win32::tstring>()};
  int m_enterCount = 0;

  //
  // タスクトレイアイコン
  //
  BOOL add_tasktray_icon(HICON hIcon) {
    auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
    nid.hWnd = get_window().get();
    nid.uID = TASKTRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TASKTRAY;
    nid.hIcon = hIcon;
    LoadString(get_window().get_instance(), IDS_TASKTRAY_TIP, nid.szTip, std::size(nid.szTip));
    return Shell_NotifyIcon(NIM_ADD, &nid);
  }
  void delete_tasktray_icon() {
    auto nid = Win32::make_sized_pod<NOTIFYICONDATA>();
    nid.hWnd = get_window().get();
    nid.uID = TASKTRAY_ID;
    Shell_NotifyIcon(NIM_DELETE, &nid);
  }


  //
  // ポップアップメニュー
  //
  void show_popup_menu(TPMPARAMS *pTpmp = nullptr) {
    POINT point;

    GetCursorPos(&point);
    get_window().set_foreground();

    auto menu = Win32::load_menu(get_window().get_instance(), MAKEINTRESOURCE(IDM_POPUP));
    auto submenu = Win32::get_sub_menu(menu, 0);
    TrackPopupMenuEx(submenu.hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, point.x, point.y, get_window().get(), pTpmp);
  }


  //
  // ダイアログボックス上のコントロールと設定のマッピング
  //
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

  constexpr static PerOrientationSettingID VERTICAL_SETTING_ID = {
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
  constexpr static PerOrientationSettingID HORIZONTAL_SETTING_ID = {
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
  void set_radio_buttons(const RadioButtonMap<Enum, Num> &m, Enum v) {
    auto get = [this](auto id) { return get_window().get_item(id).get(); };
    for (auto const &[tag, id] : m)
      if (tag == v)
        Button_SetCheck(get(id), BST_CHECKED);
      else
        Button_SetCheck(get(id), BST_UNCHECKED);
  }

  template <typename Enum>
  void set_check_button(const CheckButtonMap<Enum> &m, Enum v) {
    Button_SetCheck(get_window().get_item(m.id).get(), m.checked == v ? BST_CHECKED : BST_UNCHECKED);
  }

  void set_monitor_number(int id, int num) {
    get_window().get_item(id).set_text(Win32::asprintf(TEXT("%d"), num));
  }

  auto make_long_integer_box_handler(LONG &stor) {
    return [this, &stor](Window, Window control, int id, int notify) {
             switch (notify) {
             case EN_CHANGE: {
               auto text = control.get_text();
               auto val = _tcstol(text.c_str(), nullptr, 10);
               if (val != stor) {
                 Log::debug(TEXT("text box %X changed: %d -> %d"), id, stor, val);
                 stor = val;
                 m_currentGlobalSetting.common.isCurrentProfileChanged = true;
                 m_isDialogChanged = true;
               }
               return TRUE;
             }
             }
             return FALSE;
           };
  }

  template <typename Enum>
  auto make_check_button_handler(const CheckButtonMap<Enum> &m, Enum &stor, bool isGlobal = false) {
    return [this, &m, &stor, isGlobal](Window, Window control, int id, int notify) {
             switch (notify) {
             case BN_CLICKED: {
               auto val = Button_GetCheck(control.get()) == BST_CHECKED ? m.checked : m.unchecked;
               if (val != stor) {
                 Log::debug(TEXT("check box %X changed: %d -> %d"), id, static_cast<int>(stor) , static_cast<int>(val));
                 stor = val;
                 if (!isGlobal)
                   m_currentGlobalSetting.common.isCurrentProfileChanged = true;
                 m_isDialogChanged = true;
               }
               return TRUE;
             }
             }
             return FALSE;
           };
  }

  template <typename Enum, std::size_t Num>
  auto make_radio_button_map(const RadioButtonMap<Enum, Num> &m, Enum &stor) {
    return [this, &m, &stor](Window, Window control, int cid, int notify) {
             switch (notify) {
             case BN_CLICKED: {
               for (auto const &[tag, id] : m) {
                 if (id == cid && tag != stor) {
                   Log::debug(TEXT("radio button %X changed: %d -> %d"), cid, static_cast<int>(stor), static_cast<int>(tag));
                   stor = tag;
                   m_currentGlobalSetting.common.isCurrentProfileChanged = true;
                   m_isDialogChanged = true;
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
    return [this, id, f](Window, Window control, int, int notify) {
             switch (notify) {
             case BN_CLICKED: {
               [[maybe_unused]] auto [housekeeper, menu] = f();
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

  void update_per_orientation_settings(const PerOrientationSettingID &ids, UmapitaSetting::PerOrientation &setting) {
    auto get = [this](auto id) { return get_window().get_item(id); };
    auto setint = [get](auto id, int v) {
                    get(id).set_text(Win32::asprintf(TEXT("%d"), v));
                  };
    setint(ids.monitorNumber, setting.monitorNumber);
    set_check_button(ids.isConsiderTaskbar, setting.isConsiderTaskbar);
    set_radio_buttons(ids.windowArea, setting.windowArea);
    setint(ids.size, setting.size);
    set_radio_buttons(ids.axis, setting.axis);
    set_radio_buttons(ids.origin, setting.origin);
    setint(ids.offsetX, setting.offsetX);
    setint(ids.offsetY, setting.offsetY);
  }

  using Win32::Dialog::Template<MainDialogBox>::register_message;
  using Win32::Dialog::Template<MainDialogBox>::register_command;
  template <typename Enum, typename H>
  void register_command(const CheckButtonMap<Enum> &m, H h) {
    register_command(m.id, h);
  }

  template <typename Enum, std::size_t Num, typename H>
  void register_command(const RadioButtonMap<Enum, Num> &m, H h) {
    for (auto const &[tag, id] : m)
      register_command(id, h);
  }

  void init_per_orientation_settings(const PerOrientationSettingID &ids, UmapitaSetting::PerOrientation &setting) {
    register_command(ids.monitorNumber, make_long_integer_box_handler(setting.monitorNumber));
    register_command(ids.isConsiderTaskbar, make_check_button_handler(ids.isConsiderTaskbar, setting.isConsiderTaskbar));
    register_command(ids.windowArea, make_radio_button_map(ids.windowArea, setting.windowArea));
    register_command(ids.size, make_long_integer_box_handler(setting.size));
    register_command(ids.axis, make_radio_button_map(ids.axis, setting.axis));
    register_command(ids.origin, make_radio_button_map(ids.origin, setting.origin));
    register_command(ids.offsetX, make_long_integer_box_handler(setting.offsetX));
    register_command(ids.offsetY, make_long_integer_box_handler(setting.offsetY));
    register_command(ids.selectMonitor.id,
                     make_menu_button_handler(ids.selectMonitor.id,
                                              [this, ids, &setting]() {
                                                Log::debug(TEXT("selectMonitor received"));
                                                auto menu = Win32::create_popup_menu();
                                                int id = ids.selectMonitor.base;
                                                m_monitors.enum_monitors(
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

  void update_profile_text() {
    Win32::tstring buf;
    auto item = get_window().get_item(IDC_SELECT_PROFILE);
    auto hInst = get_window().get_instance();

    if (m_currentGlobalSetting.common.currentProfileName.empty())
      buf = Win32::load_string(hInst, IDS_NEW_PROFILE);
    else {
      buf = m_currentGlobalSetting.common.currentProfileName;
      ComboBox_SelectString(item.get(), -1, buf.c_str());
    }

    if (m_currentGlobalSetting.common.isCurrentProfileChanged)
      buf += Win32::load_string(hInst, IDS_CHANGED_MARK);

    item.set_text(buf);
  }

  void update_profile() {
    auto item = get_window().get_item(IDC_SELECT_PROFILE);
    auto ps = UmapitaRegistry::enum_profile();

    Umapita::fill_string_list_to_combobox(item, ps);
    item.enable(!ps.empty());
    update_profile_text();
  }

  void select_profile(int n) {
    auto item = get_window().get_item(IDC_SELECT_PROFILE);
    auto len = ComboBox_GetLBTextLen(item.get(), n);
    if (len == CB_ERR) {
      Log::info(TEXT("profile %d is not valid"), static_cast<int>(n));
      return;
    }
    auto str = Win32::get_sz(len, [item, n](LPTSTR buf, std::size_t len) { ComboBox_GetLBText(item.get(), n, buf); });
    item.set_text(str);
    get_window().post(WM_CHANGE_PROFILE, 0, item.to<LPARAM>());
    // テキストがセレクトされるのがうっとうしいのでクリアする
    item.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
  }

  void update_per_orientation_lock_status(const PerOrientationSettingID &ids, bool isLocked) {
    auto set = [this, isLocked](auto id) { get_window().get_item(id).enable(!isLocked); };
    auto set_radio = [this, isLocked](const auto &m) {
                       for ([[maybe_unused]] auto const &[tag, id] : m) {
                         get_window().get_item(id).enable(!isLocked);
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

  void update_lock_status() {
    auto isLocked = m_currentGlobalSetting.currentProfile.isLocked;
    update_per_orientation_lock_status(VERTICAL_SETTING_ID, isLocked);
    update_per_orientation_lock_status(HORIZONTAL_SETTING_ID, isLocked);
  }

  void update_main_controlls() {
    auto &setting = m_currentGlobalSetting;
    set_check_button(make_bool_check_button_map(IDC_ENABLED), setting.common.isEnabled);
    update_profile();
    update_per_orientation_settings(VERTICAL_SETTING_ID, setting.currentProfile.vertical);
    update_per_orientation_settings(HORIZONTAL_SETTING_ID, setting.currentProfile.horizontal);
    update_lock_status();
  }

  int save_as() {
    auto &s = m_currentGlobalSetting;
    auto [ret, profileName] = UmapitaSaveDialogBox::open(get_window(), UmapitaSaveDialogBox::Save, s.common.currentProfileName);
    if (ret == IDCANCEL) {
      Log::debug(TEXT("save as: canceled"));
      return IDCANCEL;
    }
    s.common.currentProfileName = profileName;
    UmapitaRegistry::save_setting(s.common.currentProfileName, s.currentProfile);
    s.common.isCurrentProfileChanged = false;
    return IDOK;
  }

  int save() {
    auto &s = m_currentGlobalSetting;
    Log::debug(TEXT("IDC_SAVE received"));

    if (s.common.currentProfileName.empty())
      return save_as();

    if (!s.common.isCurrentProfileChanged) {
      Log::debug(TEXT("unnecessary to save"));
      return IDOK;
    }
    UmapitaRegistry::save_setting(s.common.currentProfileName, s.currentProfile);
    s.common.isCurrentProfileChanged = false;
    return IDOK;
  }

  int confirm_save() {
    if (!m_currentGlobalSetting.common.isCurrentProfileChanged) {
      Log::debug(TEXT("unnecessary to save"));
      return IDOK;
    }
    get_window().show(SW_SHOW);
    auto hInst = get_window().get_instance();
    auto ret = Win32::open_message_box_in_center(get_window(),
                                                 Win32::load_string(hInst, IDS_CONFIRM_SAVE),
                                                 Win32::load_string(hInst, IDS_CONFIRM),
                                                 MB_YESNOCANCEL);
    switch (ret) {
    case IDYES:
      return save();
    case IDNO:
      return IDOK;
    }
    return IDCANCEL;
  }

  void init_main_controlls() {
    auto &setting = m_currentGlobalSetting;
    {
      static constexpr auto m = make_bool_check_button_map(IDC_ENABLED);
      register_command(IDC_ENABLED, make_check_button_handler(m, setting.common.isEnabled, true));
    }

    {
      // IDC_SELECT_PROFILE のエディットボックス部分を編集不可にする
      // EnableWindow だとなぜかプルダウンメニューが出なくなってしまうのでダメ
      auto cbi = Win32::make_sized_pod<COMBOBOXINFO>();
      GetComboBoxInfo(get_window().get_item(IDC_SELECT_PROFILE).get(), &cbi);
      Edit_SetReadOnly(cbi.hwndItem, true);
    }

    init_per_orientation_settings(VERTICAL_SETTING_ID, setting.currentProfile.vertical);
    init_per_orientation_settings(HORIZONTAL_SETTING_ID, setting.currentProfile.horizontal);

    register_command(
      IDC_SELECT_PROFILE,
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
    register_command(
      IDC_OPEN_PROFILE_MENU,
      make_menu_button_handler(
        IDC_OPEN_PROFILE_MENU,
        [this]() {
          Log::debug(TEXT("IDC_OPEN_PROFILE_MENU received"));
          auto menu = Win32::load_menu(get_window().get_instance(), MAKEINTRESOURCE(IDM_PROFILE));
          auto submenu = Win32::get_sub_menu(menu, 0);
          auto const &s = m_currentGlobalSetting;

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
    register_command(
      IDC_LOCK,
      [this]() {
        Log::debug(TEXT("IDC_LOCK received"));
        m_currentGlobalSetting.currentProfile.isLocked = !m_currentGlobalSetting.currentProfile.isLocked;
        m_currentGlobalSetting.common.isCurrentProfileChanged = true;
        m_isDialogChanged = true;
        return TRUE;
      });
    register_command(
      IDC_SAVE,
      [this]() {
        save();
        update_profile();
        return TRUE;
      });
    register_command(
      IDC_SAVE_AS,
      [this]() {
        save_as();
        update_profile();
        return TRUE;
      });
    register_command(
      IDC_RENAME,
      [this]() {
        auto &s = m_currentGlobalSetting;
        auto [ret, newProfileName] = UmapitaSaveDialogBox::open(get_window(), UmapitaSaveDialogBox::Rename, s.common.currentProfileName);
        if (ret == IDCANCEL) {
          Log::debug(TEXT("rename: canceled"));
          return TRUE;
        }
        if (newProfileName == s.common.currentProfileName) {
          Log::debug(TEXT("rename: not changed"));
          return TRUE;
        }
        s.common.currentProfileName = UmapitaRegistry::rename_profile(s.common.currentProfileName, newProfileName);
        update_profile();
        return TRUE;
      });
    register_command(
      IDC_DELETE,
      [this]() {
        auto &s = m_currentGlobalSetting;
        if (s.common.currentProfileName.empty())
          return TRUE;
        auto hInst = get_window().get_instance();
        auto ret = Win32::open_message_box_in_center(get_window(),
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
        m_isDialogChanged = true;
        update_main_controlls();
        return TRUE;
      });

    register_command(
      IDC_NEW,
      [this]() {
        auto &s = m_currentGlobalSetting;
        if (s.common.currentProfileName.empty() && !s.common.isCurrentProfileChanged) {
          Log::debug(TEXT("unnecessary to do"));
          return TRUE;
        }
        auto type = s.common.currentProfileName.empty() ? MB_YESNO : MB_YESNOCANCEL;
        auto hInst = get_window().get_instance();
        auto ret = Win32::open_message_box_in_center(get_window(),
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
            switch (confirm_save()) {
            case IDCANCEL:
              return TRUE;
            }
          }
          s.currentProfile = UmapitaSetting::DEFAULT_PER_PROFILE;
          s.common.isCurrentProfileChanged = false;
          break;
        }
        s.common.currentProfileName = TEXT("");
        m_isDialogChanged = true;
        update_main_controlls();
        return TRUE;
      });
    for (int id=IDC_SEL_BEGIN; id<=IDC_SEL_END; id++)
      register_command(id,
                       [this](Window /*dialog*/, Window /*control*/, int id, int /*notify*/) {
                         select_profile(id-IDC_SEL_BEGIN);
                         return TRUE;
                       });

    update_main_controlls();
  }

  void update_target_status_text(const Umapita::TargetStatus &ts) {
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
                             Win32::load_string(get_window().get_instance(), isHorizontal ? IDS_HORIZONTAL:IDS_VERTICAL).c_str());
    }
    get_window().get_item(IDC_TARGET_STATUS).set_text(text);
    m_verticalGroupBox.set_selected(isVertical);
    m_horizontalGroupBox.set_selected(isHorizontal);
  }

  INT_PTR dialog_proc(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 再入カウンタ - モーダルダイアログが開いているかどうかを検知するために用意している。
    // モーダルダイアログが開いている時にメッセージを処理しようとすると 1 よりも大きくなる。
    // ただし、それ以外にも 1 より大きくなるケースがあるため、使い方に注意する必要がある。
    // eg. コントロールを変更すると親のウィンドウに WM_COMMAND などを SendMessage してくることがある。
    m_enterCount++;
    auto deleter = [](int *rcount) { (*rcount)--; };
    std::unique_ptr<int, decltype (deleter)> auto_decr{&m_enterCount, deleter};
    return Win32::Dialog::Template<MainDialogBox>::dialog_proc(window, msg, wParam, lParam);
  }

  MessageHandlers::MaybeResult h_initdialog() {
    // override wndproc for group boxes.
    m_verticalGroupBox.override_window_proc(get_window().get_item(IDC_V_GROUPBOX));
    m_horizontalGroupBox.override_window_proc(get_window().get_item(IDC_H_GROUPBOX));
    // add "quit" to system menu, individual to "close".
    HMENU hMenu = get_window().get_system_menu();
    AppendMenu(hMenu, MF_SEPARATOR, -1, nullptr);
    AppendMenu(hMenu, MF_ENABLED | MF_STRING, IDC_QUIT, Win32::load_string(get_window().get_instance(), IDS_QUIT).c_str());
    // disable close button / menu
    EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
    //
    m_currentGlobalSetting = UmapitaRegistry::load_global_setting();
    init_main_controlls();
    register_command(
      IDC_HIDE,
      [](Window dialog) {
        Log::debug(TEXT("IDC_HIDE received"));
        dialog.show(SW_HIDE);
        return TRUE;
      });
    register_command(
      IDC_QUIT,
      [this]() {
        Log::debug(TEXT("IDC_QUIT received"));
        UmapitaRegistry::save_global_setting(m_currentGlobalSetting);
        delete_tasktray_icon();
        get_window().kill_timer(TIMER_ID);
        get_window().destroy();
        return TRUE;
      });
    register_command(
      IDC_SHOW,
      [](Window dialog) {
        Log::debug(TEXT("IDC_SHOW received"));
        dialog.show(SW_SHOW);
        dialog.set_foreground();
        return TRUE;
      });
    for (auto i=0; i<MAX_AVAILABLE_MONITORS+2; i++) {
      register_command(
        IDM_V_MONITOR_BASE+i,
        [this](Window, Window, int id, int) {
          set_monitor_number(IDC_V_MONITOR_NUMBER, id - IDM_V_MONITOR_BASE - 1);
          return TRUE;
        });
      register_command(
        IDM_H_MONITOR_BASE+i,
        [this](Window, Window, int id, int) {
          set_monitor_number(IDC_H_MONITOR_NUMBER, id - IDM_H_MONITOR_BASE - 1);
          return TRUE;
        });
    }
    get_window().post(s_msgTaskbarCreated, 0, 0);

    get_window().post(WM_DISPLAYCHANGE, 0, 0);

    m_isDialogChanged = true;
    get_window().post(WM_TIMER, TIMER_ID, 0);

    return TRUE;
  }

  MessageHandlers::MaybeResult h_tasktray(Window, UINT, WPARAM, LPARAM lParam) {
    switch (lParam) {
    case WM_RBUTTONDOWN: {
      TPMPARAMS tpmp = Win32::make_sized_pod<TPMPARAMS>(), *pTpmp = nullptr;
      if (auto shell = Window::find(TEXT("Shell_TrayWnd"), nullptr); shell) {
        auto rect = shell.get_window_rect();
        tpmp.rcExclude = rect;
        pTpmp = &tpmp;
      }
      show_popup_menu(pTpmp);
      return TRUE;
    }

    case WM_LBUTTONDOWN:
      // show / hide main dialog
      if (get_window().is_visible())
        get_window().post(WM_SYSCOMMAND, SC_MINIMIZE, 0);
      else
        get_window().post(WM_COMMAND, IDC_SHOW, 0);
      return TRUE;
    }
    return FALSE;
  }

  MessageHandlers::MaybeResult h_timer() {
    if (m_isDialogChanged) {
      update_lock_status();
      update_profile_text();
      m_lastTargetStatus = Umapita::TargetStatus{};
      m_isDialogChanged = false;
    }
    if (auto ts = Umapita::TargetStatus::get(TARGET_WINDOW_CLASS, TARGET_WINDOW_NAME); ts != m_lastTargetStatus) {
      m_lastTargetStatus = ts;
      if (m_currentGlobalSetting.common.isEnabled)
        m_lastTargetStatus.adjust(m_monitors, m_currentGlobalSetting.currentProfile);
      update_target_status_text(m_lastTargetStatus);
      // キーフックの調整
      if (KeyHook::is_available()) {
        bool isEn = KeyHook::is_enabled();
        if (!isEn && ts.window && m_currentGlobalSetting.common.isEnabled) {
          // 調整が有効なのにキーフックが無効な時はキーフックを有効にする
          auto ret = KeyHook::enable(get_window().get(), WM_KEYHOOK, ts.window.get_thread_process_id().first);
          Log::info(TEXT("enable_keyhook %hs"), ret ? "succeeded":"failed");
        } else if (isEn && (!m_currentGlobalSetting.common.isEnabled || !ts.window)) {
          // 調整が無効なのにキーフックが有効な時はキーフックを無効にする
          auto ret = KeyHook::disable();
          Log::info(TEXT("disable_keyhook %hs"), ret ? "succeeded":"failed");
        }
      }
    }
    get_window().set_timer(TIMER_ID, TIMER_PERIOD, nullptr);
    return TRUE;
  }

  MessageHandlers::MaybeResult h_setfont(Window, UINT, WPARAM wParam, LPARAM) {
    // ダイアログのフォントを明示的に設定していると呼ばれる
    auto hFont = reinterpret_cast<HFONT>(wParam);
    m_verticalGroupBox.set_font(hFont);
    m_horizontalGroupBox.set_font(hFont);
    return TRUE;
  }

  MessageHandlers::MaybeResult h_change_profile(Window, UINT, WPARAM, LPARAM lParam) {
    auto control = Window::from(lParam);
    auto n = control.get_text();
    update_profile_text(); // ユーザの選択で変更されたエディットボックスの内容を一旦戻す（この後で再設定される）
    switch (confirm_save()) {
    case IDOK: {
      Log::debug(TEXT("selected: %ls"), n.c_str());
      m_currentGlobalSetting.common.currentProfileName = n;
      m_currentGlobalSetting.currentProfile = UmapitaRegistry::load_setting(n);
      m_currentGlobalSetting.common.isCurrentProfileChanged = false;
      break;
    }
    case IDCANCEL:
      Log::debug(TEXT("canceled"));
      break;
    }
    m_isDialogChanged = true;
    update_main_controlls();
    // テキストがセレクトされるのがうっとうしいのでクリアする
    control.post(CB_SETEDITSEL, 0, MAKELPARAM(-1, -1));
    return TRUE;
  }

  MessageHandlers::MaybeResult h_keyhook(Window dialog, UINT, WPARAM wParam, LPARAM lParam) {
    Log::debug(TEXT("WM_KEYHOOK: wParam=%X, lParam=%X, enterCount=%d"),
               static_cast<unsigned>(wParam), static_cast<unsigned>(lParam), m_enterCount);
    if (lParam & 0x80000000U) {
      // keydown
      if ((lParam & 0x20000000U) && wParam >= 0x30 && wParam <= 0x39) {
        // ALT+数字
        auto n = static_cast<WORD>(wParam & 0x0F);
        n = (n ? n : 10) - 1;
        if (m_enterCount == 1) {
          // enterCount が 1 よりも大きい場合、モーダルダイアログが開いている可能性があるので送らない。
          // モーダルダイアログが開いているときに送ると、別のモーダルダイアログが開いたり、いろいろ嫌なことが起こる。
          dialog.post(WM_COMMAND, MAKEWPARAM(n + IDC_SEL_BEGIN, 0), 0);
        }
      }
    }
    return TRUE;
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

  static void init_instance(HINSTANCE hInst) {
    if (!s_msgTaskbarCreated) {
      auto cx = GetSystemMetrics(SM_CXICON);
      auto cy = GetSystemMetrics(SM_CYICON);

      s_appIcon = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), cx, cy, 0);
      s_appIconSm = Win32::load_icon_image(hInst, MAKEINTRESOURCE(IDI_UMAPITA), 16, 16, 0);

      register_main_dialog_class(hInst);

      s_msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));
    }
  }

  static LPCTSTR get_dialog_template_name() {
    return MAKEINTRESOURCE(IDD_UMAPITA_MAIN);
  }

public:
  MainDialogBox(HINSTANCE hInst, Window owner = Window{}) {
    init_instance(hInst);
    register_message(WM_INITDIALOG, Win32::Handler::binder(*this, h_initdialog));
    register_message(
      WM_DESTROY,
      [this] {
        m_verticalGroupBox.restore_window_proc();
        m_horizontalGroupBox.restore_window_proc();
        PostQuitMessage(0);
        return TRUE;
      });
    register_message(s_msgTaskbarCreated, [this] { add_tasktray_icon(s_appIconSm.get()); return TRUE; });
    register_message(WM_TASKTRAY, Win32::Handler::binder(*this, h_tasktray));
    register_system_command(SC_MINIMIZE, [](Window dialog) { dialog.post(WM_COMMAND, IDC_HIDE, 0); return TRUE; });
    register_system_command(IDC_QUIT, [](Window dialog) { dialog.post(WM_COMMAND, IDC_QUIT, 0); return TRUE; });
    register_message(WM_RBUTTONDOWN, [this] { show_popup_menu(); return TRUE; });
    register_message(WM_TIMER, Win32::Handler::binder(*this, h_timer));
    register_message(WM_DISPLAYCHANGE, [this] { m_monitors = UmapitaMonitors{}; return TRUE; });
    register_message(WM_SETFONT, Win32::Handler::binder(*this, h_setfont));
    register_message(WM_CHANGE_PROFILE, Win32::Handler::binder(*this, h_change_profile));
    register_message(WM_KEYHOOK, Win32::Handler::binder(*this, h_keyhook));
    create_modeless(owner);
  }

  int message_loop() {
    auto hAccel = LoadAccelerators(get_window().get_instance(), MAKEINTRESOURCE(IDA_UMAPITA));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
      if (get_window().translate_accelerator(hAccel, &msg))
        continue;
      if (get_window().is_dialog_message(&msg))
        continue;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    return msg.wParam;
  }
};

Win32::Icon MainDialogBox::s_appIcon = nullptr, MainDialogBox::s_appIconSm = nullptr;
UINT MainDialogBox::s_msgTaskbarCreated = 0;


int WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow) {
  if (auto w = Window::find(TEXT(UMAPITA_MAIN_WINDOW_CLASS), nullptr); w) {
    w.post(WM_COMMAND, IDC_SHOW, 0);
    return 0;
  }

  KeyHook::load();

  return MainDialogBox{hInst}.message_loop();
}
