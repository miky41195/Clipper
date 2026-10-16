#include "ui/AudioApps.h"
#include "audio/Wasapi.h"
#include "core/Text.h"
#include <algorithm>
#include <map>

namespace clipper {

std::vector<AudioAppRow> audioAppRows(const std::vector<AppGroup>& live, const std::vector<std::string>& blacklist,
                                      const std::function<std::string(const std::string&)>& nameOf) {
  std::map<std::string, AudioAppRow> rows;
  for (const auto& g : live) rows[g.exe] = {g.exe, nameOf(g.exe), false};
  for (const auto& exe : blacklist) {
    auto& r = rows[exe];
    if (r.exe.empty()) r = {exe, appDisplayName(0, exe), false};
    r.blacklisted = true;
  }
  std::vector<AudioAppRow> out;
  for (auto& [exe, r] : rows) out.push_back(std::move(r));
  std::sort(out.begin(), out.end(), [](const AudioAppRow& a, const AudioAppRow& b) {
    return toLowerAscii(a.name) < toLowerAscii(b.name);
  });
  return out;
}

void toggleBlacklist(std::vector<std::string>& blacklist, const std::string& exe, bool blacklisted) {
  std::erase(blacklist, exe);
  if (blacklisted) blacklist.push_back(exe);
}

}  // namespace clipper
