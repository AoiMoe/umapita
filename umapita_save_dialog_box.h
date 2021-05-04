#pragma once

//
// 名前を付けて保存する or リネーム
//
struct UmapitaSaveDialogBox {
  enum Kind { Save, Rename };
private:
  AM::Win32::Window m_owner;
  Kind m_kind;
  AM::Win32::tstring m_profileName;
  UmapitaSaveDialogBox(AM::Win32::Window owner, Kind kind, AM::Win32::StrPtr oldname) : m_owner{owner}, m_kind{kind}, m_profileName{oldname.ptr} { }
  //
  static CALLBACK INT_PTR s_dialog_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
  INT_PTR dialog_proc(AM::Win32::Window window, UINT msg, WPARAM wParam, LPARAM lParam);
  UmapitaSaveDialogBox(const UmapitaSaveDialogBox &) = delete;
  UmapitaSaveDialogBox &operator = (const UmapitaSaveDialogBox &) = delete;
public:
  static std::pair<int, AM::Win32::tstring> open(AM::Win32::Window owner, Kind kind, AM::Win32::StrPtr oldname);
};
