#include <doctest/doctest.h>
#include <vector>
#include "audio/PcmSource.h"

using namespace clipper;

static std::vector<int16_t> constant(size_t frames, int16_t v) { return std::vector<int16_t>(frames * 2, v); }
static constexpr Ticks kT0 = 1'000'000'000;
static constexpr Ticks kMs = 10'000;

TEST_CASE("aligned push is pulled unchanged") {
  PcmSource s;
  const auto d = constant(480, 1000);
  s.push(d.data(), 480, kT0);
  std::vector<int16_t> out(480 * 2);
  CHECK(s.pullAt(out.data(), 480, kT0) == 480);
  CHECK(out.front() == 1000);
  CHECK(out.back() == 1000);
  CHECK(s.bufferedFrames() == 0);
}

TEST_CASE("data starting after pts is padded with leading silence") {
  PcmSource s;
  const auto d = constant(480, 1000);
  s.push(d.data(), 480, kT0 + 5 * kMs);
  std::vector<int16_t> out(480 * 2, 7);
  CHECK(s.pullAt(out.data(), 480, kT0) == 240);
  CHECK(out[0] == 0);
  CHECK(out[239 * 2] == 0);
  CHECK(out[240 * 2] == 1000);
  CHECK(s.bufferedFrames() == 240);
  CHECK(s.headTimestamp() == kT0 + 10 * kMs);
}

TEST_CASE("data older than pts is dropped") {
  PcmSource s;
  const auto d = constant(480, 1000);
  s.push(d.data(), 480, kT0 - 50 * kMs);
  std::vector<int16_t> out(480 * 2, 7);
  CHECK(s.pullAt(out.data(), 480, kT0) == 0);
  CHECK(out[0] == 0);
  CHECK(s.bufferedFrames() == 0);
}

TEST_CASE("gap between pushes becomes silence") {
  PcmSource s;
  const auto d = constant(480, 1000);
  s.push(d.data(), 480, kT0);
  s.push(d.data(), 480, kT0 + 20 * kMs);
  std::vector<int16_t> out(480 * 2);
  s.pullAt(out.data(), 480, kT0);
  CHECK(out[0] == 1000);
  s.pullAt(out.data(), 480, kT0 + 10 * kMs);
  CHECK(out[0] == 0);
  CHECK(out[479 * 2] == 0);
  s.pullAt(out.data(), 480, kT0 + 20 * kMs);
  CHECK(out[0] == 1000);
}

TEST_CASE("small jitter does not create gaps") {
  PcmSource s;
  const auto d = constant(480, 1000);
  s.push(d.data(), 480, kT0);
  s.push(d.data(), 480, kT0 + 10 * kMs + 5'000);  // 0.5 ms late
  CHECK(s.bufferedFrames() == 960);
}

TEST_CASE("backlog is capped") {
  PcmSource s(48000, 2, 4800);
  const auto d = constant(48000, 1000);
  s.push(d.data(), 48000, kT0);
  CHECK(s.bufferedFrames() == 4800);
  CHECK(s.headTimestamp() == kT0 + 900 * kMs);
}

TEST_CASE("empty source yields silence") {
  PcmSource s;
  std::vector<int16_t> out(480 * 2, 7);
  CHECK(s.pullAt(out.data(), 480, kT0) == 0);
  CHECK(out[0] == 0);
}
