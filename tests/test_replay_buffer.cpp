#include <doctest/doctest.h>
#include "BufferUtil.h"

using namespace clipper;

static BufferConfig ramConfig(double durationSec) {
  BufferConfig c;
  c.duration = seconds(durationSec);
  c.segmentLength = seconds(1);
  c.spillToDisk = false;
  return c;
}

TEST_CASE("keeps roughly the configured duration") {
  ReplayBuffer b(ramConfig(5));
  b.setTrack(videoTrack());
  feedVideo(b, 10);
  CHECK(b.bufferedDuration() >= seconds(5));
  CHECK(b.bufferedDuration() <= seconds(6.1));
}

TEST_CASE("snapshot starts at a keyframe covering the requested length") {
  ReplayBuffer b(ramConfig(10));
  b.setTrack(videoTrack());
  feedVideo(b, 10);
  const auto s = b.snapshot(seconds(3));
  const auto pk = collect(s);
  REQUIRE(!pk.empty());
  CHECK(pk.front().keyframe);
  CHECK(pk.front().pts == s.start);
  CHECK(s.start <= s.end - seconds(3));
  CHECK(s.start > s.end - seconds(4.1));
}

TEST_CASE("snapshot longer than the buffer returns everything") {
  ReplayBuffer b(ramConfig(5));
  b.setTrack(videoTrack());
  feedVideo(b, 2);
  const auto s = b.snapshot(seconds(60));
  CHECK(s.start == 0);
  CHECK(collect(s).size() == 60);
}

TEST_CASE("empty buffer gives an empty snapshot") {
  ReplayBuffer b(ramConfig(5));
  CHECK(b.snapshot(seconds(5)).empty());
}

TEST_CASE("packets before the first keyframe are dropped") {
  ReplayBuffer b(ramConfig(5));
  b.push(testPacket(kVideoTrack, 0, false));
  b.push(testPacket(1, 0, true));
  CHECK(b.bufferedDuration() == 0);
  CHECK(b.snapshot(seconds(5)).empty());
}

TEST_CASE("audio inside the window is included, audio before start is not") {
  ReplayBuffer b(ramConfig(10));
  b.setTrack(videoTrack());
  b.setTrack(audioTrack(1, "Mix"));
  const Ticks period = kTicksPerSecond / 30;
  Ticks audioPts = 0;
  for (Ticks v = 0; v < seconds(4); v += period) {
    b.push(testPacket(kVideoTrack, v, (v / period) % 30 == 0));
    for (; audioPts <= v; audioPts += seconds(0.01)) b.push(testPacket(1, audioPts, true, 10));
  }
  const auto s = b.snapshot(seconds(2));
  int audio = 0;
  for (const auto& p : collect(s))
    if (p.track == 1) {
      CHECK(p.pts >= s.start);
      ++audio;
    }
  const int expected = int((s.end - s.start) / seconds(0.01));
  CHECK(audio >= expected - 2);
  CHECK(audio <= expected + 2);
}

TEST_CASE("snapshot lists only tracks with packets in the window") {
  ReplayBuffer b(ramConfig(5));
  b.setTrack(videoTrack());
  b.setTrack(audioTrack(1, "Mix"));
  b.setTrack(audioTrack(2, "Spotify"));
  const Ticks period = kTicksPerSecond / 30;
  for (Ticks v = 0; v < seconds(10); v += period) {
    b.push(testPacket(kVideoTrack, v, (v / period) % 30 == 0));
    b.push(testPacket(1, v, true, 10));
    if (v < seconds(1)) b.push(testPacket(2, v, true, 10));
  }
  b.endTrack(2);
  const auto s = b.snapshot(seconds(5));
  REQUIRE(s.tracks.size() == 2);
  CHECK(s.tracks[0].id == kVideoTrack);
  CHECK(s.tracks[1].id == 1);
}

TEST_CASE("video format change clears the buffer") {
  ReplayBuffer b(ramConfig(10));
  b.setTrack(videoTrack(1920, 1080));
  feedVideo(b, 3);
  b.setTrack(videoTrack(1280, 720));
  CHECK(b.bufferedDuration() == 0);
  CHECK(b.snapshot(seconds(10)).empty());
}

TEST_CASE("RAM usage is bounded by duration") {
  ReplayBuffer b(ramConfig(5));
  b.setTrack(videoTrack());
  feedVideo(b, 30);
  CHECK(b.ramBytes() <= size_t(7 * 30 * 100));
}
