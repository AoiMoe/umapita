#pragma once

//
// ディスプレイモニタを収集する
//
struct UmapitaMonitors {
  struct Monitor {
    AM::Win32::tstring name;
    RECT whole;
    RECT work;
    Monitor(LPCTSTR aName, RECT aWhole, RECT aWork) : name{aName}, whole{aWhole}, work{aWork} { }
  };

private:
  std::vector<Monitor> m_monitors;

public:
  UmapitaMonitors() {
    std::vector<MONITORINFOEX> mis;

    EnumDisplayMonitors(nullptr, nullptr,
                        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) CALLBACK {
                          auto &mis = *reinterpret_cast<std::vector<MONITORINFOEX> *>(lParam);
                          auto mi = AM::Win32::make_sized_pod<MONITORINFOEX>();
                          GetMonitorInfo(hMonitor, &mi);
                          AM::Log::debug(TEXT("hMonitor=%p, szDevice=%ls, rcMonitor=(%ld,%ld)-(%ld,%ld), rcWork=(%ld,%ld)-(%ld,%ld), dwFlags=%X"),
                                         hMonitor, mi.szDevice,
                                         mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
                                         mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom,
                                         mi.dwFlags);
                          mis.emplace_back(mi);
                          return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&mis));

    // -1: whole virtual desktop
    RECT whole;
    whole.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    whole.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    whole.right = whole.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    whole.bottom = whole.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    m_monitors.emplace_back(TEXT("<all monitors>"), whole, whole);
    // 0: primary monitor
    if (auto result = std::find_if(mis.begin(), mis.end(), [](auto const &mi) { return !!(mi.dwFlags & MONITORINFOF_PRIMARY); });
        result == mis.end()) {
      // not found
      m_monitors.emplace_back(TEXT("<primary>"), whole, whole);
    } else {
      m_monitors.emplace_back(TEXT("<primary>"), result->rcMonitor, result->rcWork);
    }
    // 1-: physical monitors
    for (auto &mi : mis)
      m_monitors.emplace_back(mi.szDevice, mi.rcMonitor, mi.rcWork);
  }
  const Monitor *get_monitor_by_number(int monitorNumber) const {
    auto mn = monitorNumber + 1;

    if (mn < 0 || static_cast<size_t>(mn) >= m_monitors.size())
      return nullptr;

    return &m_monitors[mn];
  }
  template <typename Fn>
  void enum_monitors(Fn fn) {
    int index = -1;
    for (auto const &m : m_monitors)
      fn(index++, m);
  }
};
