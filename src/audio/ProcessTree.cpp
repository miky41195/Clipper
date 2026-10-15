#include "audio/ProcessTree.h"
#include <algorithm>
#include <map>
#include <unordered_map>

namespace clipper {

std::vector<AppGroup> groupAudioApps(const std::vector<uint32_t>& sessionPids, const std::vector<ProcEntry>& procs,
                                     const std::vector<std::string>& blacklist, uint32_t selfPid) {
  std::unordered_map<uint32_t, const ProcEntry*> byPid;
  for (const auto& p : procs) byPid[p.pid] = &p;
  std::map<std::string, std::vector<uint32_t>> groups;
  for (uint32_t pid : sessionPids) {
    const auto it = byPid.find(pid);
    if (pid == 0 || pid == selfPid || it == byPid.end()) continue;
    const ProcEntry* p = it->second;
    if (std::find(blacklist.begin(), blacklist.end(), p->exe) != blacklist.end()) continue;
    for (int step = 0; step < 64; ++step) {
      const auto parent = byPid.find(p->parent);
      if (p->parent == p->pid || parent == byPid.end() || parent->second->exe != p->exe) break;
      p = parent->second;
    }
    if (p->pid == selfPid) continue;
    auto& roots = groups[p->exe];
    if (std::find(roots.begin(), roots.end(), p->pid) == roots.end()) roots.push_back(p->pid);
  }
  std::vector<AppGroup> out;
  for (auto& [exe, roots] : groups) out.push_back({exe, std::move(roots)});
  return out;
}

}  // namespace clipper
