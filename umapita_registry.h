#pragma once

namespace UmapitaRegistry {

UmapitaSetting::PerProfile load_setting(AM::Win32::StrPtr profileName);
void save_setting(AM::Win32::StrPtr profileName, const UmapitaSetting::PerProfile &s);
UmapitaSetting::Global load_global_setting();
void save_global_setting(const UmapitaSetting::Global &s);
std::vector<AM::Win32::tstring> enum_profile();
void delete_profile(AM::Win32::StrPtr name);
AM::Win32::tstring rename_profile(AM::Win32::StrPtr oldName, AM::Win32::StrPtr newName);

} // namespace UmapitaRegistry
