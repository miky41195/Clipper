#include <doctest/doctest.h>
#include "core/SizeEstimate.h"

using namespace clipper;

TEST_CASE("estimateBytes = (video + tracks*audio) kbps * seconds / 8") {
  CHECK(estimateBytes({25000, 160, 3, 60.0}) == 191'100'000ull);
  CHECK(estimateBytes({8000, 160, 0, 1.0}) == 1'000'000ull);
}

TEST_CASE("estimateBuffer below threshold lives in RAM") {
  const SizeInputs in{25000, 160, 3, 60.0};
  const auto b = estimateBuffer(in, 120.0);
  CHECK(b.diskBytes == 0);
  CHECK(b.ramBytes == estimateBytes({25000, 160, 3, 61.0}));
}

TEST_CASE("estimateBuffer above threshold spills to disk") {
  const SizeInputs in{25000, 160, 3, 600.0};
  const auto b = estimateBuffer(in, 120.0);
  CHECK(b.ramBytes == estimateBytes({25000, 160, 3, kDiskSegmentSeconds}));
  CHECK(b.diskBytes == estimateBytes({25000, 160, 3, 610.0}));
}

TEST_CASE("presetVideoKbps scales with pixels*fps and codec efficiency") {
  CHECK(presetVideoKbps(Quality::High, Codec::H264, 1920, 1080, 60) == 25000);
  CHECK(presetVideoKbps(Quality::Medium, Codec::H264, 1920, 1080, 60) == 15000);
  CHECK(presetVideoKbps(Quality::High, Codec::AV1, 2560, 1440, 60) == 31100);
  CHECK(presetVideoKbps(Quality::Low, Codec::H264, 1280, 720, 30) == 1800);
  CHECK(presetVideoKbps(Quality::Low, Codec::HEVC, 320, 240, 30) == 1000);  // floor
}

TEST_CASE("formatBytes picks a readable unit") {
  CHECK(formatBytes(512'000) == "512 KB");
  CHECK(formatBytes(191'100'000) == "191 MB");
  CHECK(formatBytes(1'400'000'000) == "1.4 GB");
}
