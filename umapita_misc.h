#pragma once

namespace Umapita {

inline void fill_string_list_to_combobox(AM::Win32::Window cb, const std::vector<AM::Win32::tstring> ss) {
  ComboBox_ResetContent(cb.get());

  for (auto const &name : ss) {
    ComboBox_AddString(cb.get(), name.c_str());
  }
}

} // namespace Umapita
