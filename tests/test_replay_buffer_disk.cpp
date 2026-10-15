#include <doctest/doctest.h>
#include <cstring>
#include <fstream>
#include "BufferUtil.h"
#include "TestUtil.h"

using namespace clipper;
namespace fs = std::filesystem;

static BufferConfig diskConfig(const fs::path& dir, double durationSec) {
  BufferConfig c;
  c.duration = seconds(durationSec);
  c.segmentLength = seconds(1);
  c.spillToDisk = true;
  c.dir = dir;
  c.fallbackDuration = seconds(2);
  return c;
}

static size_t fileCount(const fs::path& dir) {
  size_t n = 0;
  for (const auto& e : fs::directory_iterator(dir)) n += e.is_regular_file() ? 1 : 0;
  return n;
}

TEST_CASE("closed segments are spilled and RAM stays small") {
  const auto dir = freshTempDir("buf_spill");
  ReplayBuffer b(diskConfig(dir, 5));
  b.setTrack(videoTrack());
  feedVideo(b, 8);
  b.waitIdle();
  CHECK(fileCount(dir) >= 4);
  CHECK(b.ramBytes() <= size_t(2 * 30 * 100));
}

TEST_CASE("snapshot from disk is byte exact and ordered") {
  const auto dir = freshTempDir("buf_exact");
  ReplayBuffer b(diskConfig(dir, 20));
  b.setTrack(videoTrack());
  feedVideo(b, 6);
  b.waitIdle();
  const auto s = b.snapshot(seconds(4));
  const auto pk = collect(s);
  REQUIRE(!pk.empty());
  CHECK(pk.front().keyframe);
  Ticks prev = -1;
  for (const auto& p : pk) {
    Ticks encoded = 0;
    std::memcpy(&encoded, p.data->data(), sizeof(encoded));
    CHECK(encoded == p.pts);
    CHECK(p.data->size() == 100);
    CHECK(p.pts > prev);
    prev = p.pts;
  }
}

TEST_CASE("snapshot keeps evicted files alive until released") {
  const auto dir = freshTempDir("buf_alive");
  ReplayBuffer b(diskConfig(dir, 3));
  b.setTrack(videoTrack());
  Ticks next = feedVideo(b, 4);
  b.waitIdle();
  auto s = std::make_unique<ReplayBuffer::Snapshot>(b.snapshot(seconds(3)));
  const size_t before = collect(*s).size();
  next = feedVideo(b, 10, next);
  b.waitIdle();
  CHECK(collect(*s).size() == before);
  const size_t filesWithSnapshot = fileCount(dir);
  s.reset();
  feedVideo(b, 1, next);
  b.waitIdle();
  CHECK(fileCount(dir) < filesWithSnapshot);
}

TEST_CASE("destroying the buffer removes its files") {
  const auto dir = freshTempDir("buf_cleanup");
  {
    ReplayBuffer b(diskConfig(dir, 5));
    b.setTrack(videoTrack());
    feedVideo(b, 4);
    b.waitIdle();
    CHECK(fileCount(dir) > 0);
  }
  CHECK(fileCount(dir) == 0);
}

TEST_CASE("spill failure falls back to RAM with a warning") {
  const auto base = freshTempDir("buf_fail");
  const auto notADir = base / "file";
  std::ofstream(notADir) << "x";
  int warnings = 0;
  ReplayBuffer b(diskConfig(notADir, 10), [&](const std::string&) { ++warnings; });
  b.setTrack(videoTrack());
  feedVideo(b, 6);
  b.waitIdle();
  CHECK(warnings == 1);
  CHECK(b.bufferedDuration() <= seconds(3.1));
  CHECK_FALSE(b.snapshot(seconds(2)).empty());
}
