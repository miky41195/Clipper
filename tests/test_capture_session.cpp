#include <doctest/doctest.h>
#include "Ffprobe.h"
#include "TestUtil.h"
#include "output/ClipWriter.h"
#include "session/CaptureSession.h"

using namespace clipper;

TEST_CASE("bufferConfigFor picks RAM or disk by threshold") {
  Settings s;
  s.replaySeconds = 60;
  s.ramThresholdSeconds = 120;
  auto c = bufferConfigFor(s, "x");
  CHECK_FALSE(c.spillToDisk);
  CHECK(c.segmentLength == seconds(1));
  s.replaySeconds = 600;
  c = bufferConfigFor(s, "x");
  CHECK(c.spillToDisk);
  CHECK(c.segmentLength == seconds(10));
  CHECK(c.duration == seconds(600));
}

TEST_SUITE("capture") {
  TEST_CASE("live session produces a clip with video and audio tracks") {
    Settings s;
    s.codec = Codec::H264;
    s.outputHeight = 720;
    s.replaySeconds = 10;
    s.tempDir = pathToUtf8(freshTempDir("session_buf"));
    std::string warn;
    CaptureSession session(s, [&](const std::string& w) { warn = w; });
    Sleep(3000);
    const auto snap = session.snapshot(seconds(2));
    const auto dir = freshTempDir("session_out");
    writeSnapshot(snap, dir / "live.mp4", "mp4");
    const auto j = ffprobe(dir / "live.mp4");
    MESSAGE("streams: " << j["streams"].size() << " warn: " << warn);
    for (const auto& st : j["streams"]) MESSAGE(st["codec_name"].get<std::string>() << " " << streamTag(st, "handler_name"));
    REQUIRE(j["streams"].size() >= 2);
    CHECK(j["streams"][0]["codec_name"] == "h264");
    CHECK(streamTag(j["streams"][1], "handler_name") == "Mix");
    CHECK(probeDuration(j) >= 1.9);
  }
}
