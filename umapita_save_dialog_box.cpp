#include "pch.h"
#include "am/win32util.h"
#include "umapita_def.h"
#include "umapita_misc.h"
#include "umapita_save_dialog_box.h"
#include "umapita_setting.h"
#include "umapita_registry.h"
#include "umapita_res.h"

namespace Win32 = AM::Win32;
using AM::Log;
using Win32::Window;

CALLBACK INT_PTR UmapitaSaveDialogBox::s_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  Window window{hWnd};
  if (msg == WM_INITDIALOG) {
    window.set_dialog_user_data(lParam);
  }
  auto self = window.get_dialog_user_data<UmapitaSaveDialogBox *>();
  return self ? self->dialog_proc(window, msg, wParam, lParam) : FALSE;
}

INT_PTR UmapitaSaveDialogBox::dialog_proc(Window window, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    auto hInst = window.get_instance();
    Umapita::fill_string_list_to_combobox(window.get_item(IDC_SELECT_PROFILE), UmapitaRegistry::enum_profile());
    window.get_item(IDC_SELECT_PROFILE).set_text(m_profileName);
    window.get_item(IDOK).enable(false);
    window.set_text(Win32::load_string(hInst, Save ? IDS_SAVE_AS_TITLE : IDS_RENAME_TITLE));
    auto detail = Win32::load_string(hInst, m_kind == Save ? IDS_SAVE_AS_DETAIL : IDS_RENAME_DETAIL);
    window.get_item(IDC_SAVE_DETAIL).set_text(Win32::asprintf(detail, m_profileName.c_str()));
    Win32::center_popup(window, m_owner);
    return TRUE;
  }

  case WM_COMMAND: {
    UINT id = LOWORD(wParam);

    switch (id) {
    case IDOK: {
      auto n = Win32::remove_ws_on_both_ends(window.get_item(IDC_SELECT_PROFILE).get_text());
      if (n.empty())
        return TRUE;
      if (UmapitaRegistry::is_profile_existing(n)) {
        auto hInst = window.get_instance();
        auto r = Win32::open_message_box_in_center(window,
                                                   Win32::asprintf(Win32::load_string(hInst, IDS_CONFIRM_OVERWRITE), n.c_str()),
                                                   Win32::load_string(hInst, IDS_CONFIRM),
                                                   MB_OKCANCEL);
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

std::pair<int, Win32::tstring> UmapitaSaveDialogBox::open(Window owner, UmapitaSaveDialogBox::Kind kind, Win32::StrPtr oldname) {
  UmapitaSaveDialogBox sdb{owner, kind, oldname};
  auto r = DialogBoxParam(owner.get_instance(),
                          MAKEINTRESOURCE(IDD_SAVE),
                          owner.get(),
                          &UmapitaSaveDialogBox::s_dialog_proc,
                          reinterpret_cast<LPARAM>(&sdb));
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
