#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace clipper {

struct ProcEntry {
  uint32_t pid = 0;
  uint32_t parent = 0;
  std::string exe;  // lowercase
};

struct AppGroup {
  std::string exe;
  std::vector<uint32_t> roots;  // top-most ancestors with the same exe; capture each with "include tree"
};

std::vector<AppGroup> groupAudioApps(const std::vector<uint32_t>& sessionPids, const std::vector<ProcEntry>& procs,
                                     const std::vector<std::string>& blacklist, uint32_t selfPid);

}  // namespace clipper
