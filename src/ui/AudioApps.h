#pragma once
#include "audio/ProcessTree.h"
#include <functional>
#include <string>
#include <vector>

namespace clipper {

struct AudioAppRow {
  std::string exe;
  std::string name;
  bool blacklisted = false;
};

// Live audio apps plus blacklisted ones (so they can be re-enabled), deduped and sorted by name.
std::vector<AudioAppRow> audioAppRows(const std::vector<AppGroup>& live, const std::vector<std::string>& blacklist,
                                      const std::function<std::string(const std::string& exe)>& nameOf);
void toggleBlacklist(std::vector<std::string>& blacklist, const std::string& exe, bool blacklisted);

}  // namespace clipper
