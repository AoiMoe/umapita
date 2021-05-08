#pragma once

//
// 名前を付けて保存する or リネーム
//
struct UmapitaSaveDialogBox : public AM::Win32::Dialog::Template<UmapitaSaveDialogBox> {
  friend class AM::Win32::Dialog::Template<UmapitaSaveDialogBox>;
  enum Kind { Save, Rename };
private:
  Kind m_kind;
  AM::Win32::tstring m_profileName;
  UmapitaSaveDialogBox(Kind kind, AM::Win32::StrPtr oldname);
  static LPCTSTR get_dialog_template_name();
public:
  static std::pair<int, AM::Win32::tstring> open(AM::Win32::Window owner, Kind kind, AM::Win32::StrPtr oldname);
};
