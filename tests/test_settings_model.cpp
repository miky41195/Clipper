#include <doctest/doctest.h>
#include "ui/I18n.h"
#include "ui/SettingsModel.h"

using namespace clipper;

TEST_CASE("form tracks dirty state and reverts") {
  SettingsForm f(Settings{});
  CHECK_FALSE(f.dirty());
  f.draft.fps = 144;
  CHECK(f.dirty());
  f.revert();
  CHECK(f.draft.fps == 60);
  CHECK_FALSE(f.dirty());
  f.draft.replaySeconds = 300;
  f.markSaved();
  CHECK_FALSE(f.dirty());
  CHECK(f.saved.replaySeconds == 300);
}

TEST_CASE("duration labels") {
  setLanguage("it");
  CHECK(durationLabel(45) == "45 s");
  CHECK(durationLabel(60) == "1 min");
  CHECK(durationLabel(150) == "2 min 30 s");
  CHECK(durationLabel(1200) == "20 min");
}

TEST_CASE("estimate line for 1440p AV1 high, 60 s, 4 audio tracks") {
  setLanguage("it");
  Settings s;
  s.codec = Codec::AV1;
  s.quality = Quality::High;
  s.replaySeconds = 60;
  CHECK(estimateLine(s, 2560, 1440, 4) == "≈ 238 MB per una clip da 1 min");
  CHECK(recordingLine(s, 2560, 1440, 4) == "≈ 238 MB al minuto in registrazione");
}

TEST_CASE("buffer line switches from RAM to disk at the threshold") {
  setLanguage("it");
  Settings s;
  s.codec = Codec::AV1;
  s.replaySeconds = 60;
  s.ramThresholdSeconds = 120;
  CHECK(bufferLine(s, 2560, 1440, 4) == "Buffer in RAM ≈ 242 MB");
  s.replaySeconds = 600;
  CHECK(bufferLine(s, 2560, 1440, 4) == "Buffer su disco ≈ 2.4 GB · RAM ≈ 40 MB");
}

TEST_CASE("estimate follows output resolution") {
  setLanguage("it");
  Settings s;
  s.codec = Codec::H264;
  s.quality = Quality::High;
  s.outputHeight = 1080;
  s.replaySeconds = 60;
  // 1080p60 H.264 high = 25000 kbps; 2 tracks of 160 -> 25320 * 125 * 60
  CHECK(estimateLine(s, 2560, 1440, 2) == "≈ 190 MB per una clip da 1 min");
}
