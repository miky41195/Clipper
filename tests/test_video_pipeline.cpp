#include <doctest/doctest.h>
#include <mutex>
#include "capture/Monitors.h"
#include "capture/VideoPipeline.h"

using namespace clipper;

TEST_SUITE("capture") {
  TEST_CASE("captures the primary monitor at the requested fps") {
    const auto g = createGpuDevice();
    const auto mons = listMonitors();
    REQUIRE(!mons.empty());
    std::vector<Packet> pk;
    std::mutex mu;
    TrackInfo ti;
    std::string warn;
    {
      VideoPipeline vp(
          g, {mons[0].handle, 720, 60, Codec::H264, Quality::Medium, 0, true},
          [&](Packet p) {
            std::lock_guard lk(mu);
            pk.push_back(std::move(p));
          },
          [&](const TrackInfo& t) {
            std::lock_guard lk(mu);
            ti = t;
          },
          [&](const std::string& w) { warn = w; });
      Sleep(2000);
    }
    std::lock_guard lk(mu);
    MESSAGE("packets: " << pk.size() << " warn: " << warn);
    CHECK(ti.height == 720);
    CHECK(ti.width % 2 == 0);
    CHECK(pk.size() >= 100);
    REQUIRE(!pk.empty());
    CHECK(pk.front().keyframe);
  }
}
