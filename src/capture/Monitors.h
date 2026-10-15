#pragma once
#include <windows.h>
#include <string>
#include <utility>
#include <vector>

namespace clipper {

struct MonitorInfo {
  int index = 0;
  HMONITOR handle = nullptr;
  std::wstring device;
  int width = 0;
  int height = 0;
  bool primary = false;
};

std::vector<MonitorInfo> listMonitors();  // primary first
std::pair<int, int> computeOutputSize(int width, int height, int outputHeight);
const MonitorInfo* pickMonitor(const std::vector<MonitorInfo>& monitors, int index);

}  // namespace clipper
