#include "pch.h"
#include "am/win32util.h"
#include "am/win32handler.h"
#include "am/win32dialog.h"
#include "umapita_def.h"
#include "umapita_misc.h"
#include "umapita_save_dialog_box.h"
#include "umapita_setting.h"
#include "umapita_registry.h"
#include "umapita_res.h"

namespace Win32 = AM::Win32;
using AM::Log;
using Win32::Window;

UmapitaSaveDialogBox::UmapitaSaveDialogBox(Kind kind, AM::Win32::StrPtr oldname) : m_kind(kind), m_profileName(oldname.ptr) {
  using namespace std::placeholders;
  register_message(
    WM_INITDIALOG,
    [&](Window dialog) {
      auto hInst = dialog.get_instance();
      Umapita::fill_string_list_to_combobox(dialog.get_item(IDC_SELECT_PROFILE), UmapitaRegistry::enum_profile());
      dialog.get_item(IDC_SELECT_PROFILE).set_text(m_profileName);
      dialog.get_item(IDOK).enable(false);
      dialog.set_text(Win32::load_string(hInst, Save ? IDS_SAVE_AS_TITLE : IDS_RENAME_TITLE));
      auto detail = Win32::load_string(hInst, m_kind == Save ? IDS_SAVE_AS_DETAIL : IDS_RENAME_DETAIL);
      dialog.get_item(IDC_SAVE_DETAIL).set_text(Win32::asprintf(detail, m_profileName.c_str()));
      Win32::center_popup(dialog, get_owner());
      return TRUE;
    });
  register_command(
    IDOK,
    [&](Window dialog) {
      auto n = Win32::remove_ws_on_both_ends(dialog.get_item(IDC_SELECT_PROFILE).get_text());
      if (n.empty())
        return TRUE;
      if (UmapitaRegistry::is_profile_existing(n)) {
        auto hInst = dialog.get_instance();
        auto r = Win32::open_message_box_in_center(
          dialog,
          Win32::asprintf(Win32::load_string(hInst, IDS_CONFIRM_OVERWRITE), n.c_str()),
          Win32::load_string(hInst, IDS_CONFIRM),
          MB_OKCANCEL);
        if (r != IDOK)
          return TRUE;
      }
      m_profileName = std::move(n);
      dialog.end_dialog(IDOK);
      return TRUE;
    });
  register_command(
    IDCANCEL,
    [&](Window dialog) {
      dialog.end_dialog(IDCANCEL);
      return TRUE;
    });
  register_command(
    IDC_SELECT_PROFILE,
    [&](Window dialog, Window control, int, int notify) {
      switch (notify) {
      case CBN_SELCHANGE:
        dialog.get_item(IDOK).enable(true);
        return TRUE;
      case CBN_EDITCHANGE: {
        auto n = Win32::remove_ws_on_both_ends(control.get_text());
        dialog.get_item(IDOK).enable(!n.empty() && m_profileName != n);
        return TRUE;
      }
      }
      return FALSE;
    });
}

LPCTSTR UmapitaSaveDialogBox::get_dialog_template_name() { return MAKEINTRESOURCE(IDD_SAVE); }

std::pair<int, Win32::tstring> UmapitaSaveDialogBox::open(Window owner, UmapitaSaveDialogBox::Kind kind, Win32::StrPtr oldname) {
  UmapitaSaveDialogBox sdb{kind, oldname};
  auto r = sdb.open_modal(owner);
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
