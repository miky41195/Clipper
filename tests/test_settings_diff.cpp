#include <doctest/doctest.h>
#include "app/SettingsDiff.h"

using namespace clipper;

TEST_CASE("captureChanged ignores UI-only fields") {
  Settings a, b;
  CHECK_FALSE(captureChanged(a, b));
  b.hotkeys.saveClip = "Ctrl+S";
  b.sounds = false;
  b.language = "en";
  b.accent = "#4C8DFF";
  b.nameTemplate = "{app}";
  b.toastCorner = "bottom-left";
  b.outputDir = "D:/x";
  b.container = "mkv";
  CHECK_FALSE(captureChanged(a, b));
}

TEST_CASE("captureChanged detects capture fields") {
  Settings a;
  for (auto mutate : std::vector<void (*)(Settings&)>{
           [](Settings& s) { s.fps = 144; }, [](Settings& s) { s.codec = Codec::H264; },
           [](Settings& s) { s.replaySeconds = 300; }, [](Settings& s) { s.audioBlacklist = {"x.exe"}; },
           [](Settings& s) { s.captureMic = false; }, [](Settings& s) { s.monitorIndex = 1; },
           [](Settings& s) { s.tempDir = "D:/b"; }, [](Settings& s) { s.mixTrack = false; }}) {
    Settings b = a;
    mutate(b);
    CHECK(captureChanged(a, b));
  }
}
