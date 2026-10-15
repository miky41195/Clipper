#include <doctest/doctest.h>
#include <algorithm>
#include "audio/ProcessTree.h"

using namespace clipper;

namespace {
const std::vector<ProcEntry> kProcs = {
    {4, 0, "system"},          {100, 4, "explorer.exe"},  {200, 100, "chrome.exe"}, {201, 200, "chrome.exe"},
    {202, 201, "chrome.exe"},  {300, 100, "firefox.exe"}, {400, 100, "firefox.exe"}, {500, 100, "spotify.exe"},
    {600, 100, "clipper.exe"}, {700, 100, "game.exe"},
};

const AppGroup* find(const std::vector<AppGroup>& g, const char* exe) {
  for (const auto& a : g)
    if (a.exe == exe) return &a;
  return nullptr;
}
}  // namespace

TEST_CASE("chrome audio child maps to the chrome root") {
  const auto g = groupAudioApps({202}, kProcs, {}, 600);
  REQUIRE(g.size() == 1);
  CHECK(g[0].exe == "chrome.exe");
  CHECK(g[0].roots == std::vector<uint32_t>{200});
}

TEST_CASE("independent processes of the same exe are separate roots of one group") {
  const auto g = groupAudioApps({300, 400}, kProcs, {}, 600);
  REQUIRE(g.size() == 1);
  auto roots = g[0].roots;
  std::sort(roots.begin(), roots.end());
  CHECK(roots == std::vector<uint32_t>{300, 400});
}

TEST_CASE("blacklist, self and unknown pids are skipped") {
  const auto g = groupAudioApps({500, 600, 700, 9999, 0}, kProcs, {"spotify.exe"}, 600);
  REQUIRE(g.size() == 1);
  CHECK(find(g, "game.exe") != nullptr);
}

TEST_CASE("duplicate session pids collapse") {
  const auto g = groupAudioApps({201, 202, 202}, kProcs, {}, 600);
  REQUIRE(g.size() == 1);
  CHECK(g[0].roots.size() == 1);
}
