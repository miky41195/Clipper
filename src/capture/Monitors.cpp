#include "capture/Monitors.h"
#include <algorithm>
#include <cmath>

namespace clipper {

std::vector<MonitorInfo> listMonitors() {
  std::vector<MonitorInfo> out;
  EnumDisplayMonitors(
      nullptr, nullptr,
      [](HMONITOR h, HDC, LPRECT, LPARAM user) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(h, &mi)) {
          MonitorInfo m;
          m.handle = h;
          m.device = mi.szDevice;
          m.width = mi.rcMonitor.right - mi.rcMonitor.left;
          m.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
          m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
          reinterpret_cast<std::vector<MonitorInfo>*>(user)->push_back(m);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
  std::stable_partition(out.begin(), out.end(), [](const MonitorInfo& m) { return m.primary; });
  for (size_t i = 0; i < out.size(); ++i) out[i].index = int(i);
  return out;
}

std::pair<int, int> computeOutputSize(int w, int h, int outputHeight) {
  if (outputHeight <= 0 || outputHeight >= h) return {w & ~1, h & ~1};
  const int ow = int(std::lround(double(w) * outputHeight / h));
  return {ow & ~1, outputHeight & ~1};
}

const MonitorInfo* pickMonitor(const std::vector<MonitorInfo>& monitors, int index) {
  if (monitors.empty()) return nullptr;
  if (index >= 0 && index < int(monitors.size())) return &monitors[size_t(index)];
  for (const auto& m : monitors)
    if (m.primary) return &m;
  return &monitors.front();
}

}  // namespace clipper
