#include <doctest/doctest.h>
#include "capture/Monitors.h"

using namespace clipper;

TEST_CASE("computeOutputSize keeps aspect, even dims, never upscales") {
  CHECK(computeOutputSize(2560, 1440, 1080) == std::pair{1920, 1080});
  CHECK(computeOutputSize(2560, 1440, 0) == std::pair{2560, 1440});
  CHECK(computeOutputSize(1920, 1080, 1440) == std::pair{1920, 1080});
  CHECK(computeOutputSize(3440, 1440, 1080) == std::pair{2580, 1080});
  CHECK(computeOutputSize(1366, 768, 720) == std::pair{1280, 720});
  CHECK(computeOutputSize(1365, 767, 0) == std::pair{1364, 766});
}

TEST_CASE("pickMonitor falls back to the primary") {
  std::vector<MonitorInfo> mons(2);
  mons[0].index = 0;
  mons[0].primary = true;
  mons[1].index = 1;
  CHECK(pickMonitor(mons, 1) == &mons[1]);
  CHECK(pickMonitor(mons, 5) == &mons[0]);
  CHECK(pickMonitor({}, 0) == nullptr);
}

TEST_CASE("listMonitors returns the primary first") {
  const auto mons = listMonitors();
  REQUIRE(!mons.empty());
  CHECK(mons[0].primary);
  CHECK(mons[0].width > 0);
}
