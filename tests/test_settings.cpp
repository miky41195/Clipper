#include <doctest/doctest.h>
#include <fstream>
#include "TestUtil.h"
#include "core/Settings.h"

using namespace clipper;
namespace fs = std::filesystem;

static void writeFile(const fs::path& p, const std::string& text) {
  std::ofstream(p, std::ios::binary) << text;
}

TEST_CASE("defaults match the spec") {
  Settings s;
  CHECK(s.fps == 60);
  CHECK(s.codec == Codec::AV1);
  CHECK(s.replaySeconds == 60);
  CHECK(s.ramThresholdSeconds == 120);
  CHECK(s.hotkeys.saveClip == "Alt+F10");
  CHECK(s.hotkeys.toggleRecord == "Alt+Shift+F9");
  CHECK(s.hotkeys.openGallery == "Alt+G");
  CHECK(s.container == "mp4");
  CHECK(s.mixTrack);
}

TEST_CASE("json round trip keeps every field") {
  Settings s;
  s.fps = 144;
  s.codec = Codec::HEVC;
  s.quality = Quality::Custom;
  s.customKbps = 40000;
  s.audioBlacklist = {"spotify.exe"};
  s.outputDir = "D:/Clip è";
  s.hotkeys.saveClip = "Ctrl+Shift+S";
  const Settings r = settingsFromJson(settingsToJson(s));
  CHECK(r.fps == 144);
  CHECK(r.codec == Codec::HEVC);
  CHECK(r.quality == Quality::Custom);
  CHECK(r.customKbps == 40000);
  CHECK(r.audioBlacklist == std::vector<std::string>{"spotify.exe"});
  CHECK(r.outputDir == "D:/Clip è");
  CHECK(r.hotkeys.saveClip == "Ctrl+Shift+S");
}

TEST_CASE("enums are stored as readable strings") {
  Settings s;
  s.codec = Codec::H264;
  s.quality = Quality::Low;
  const std::string j = settingsToJson(s);
  CHECK(j.find("\"codec\": \"h264\"") != std::string::npos);
  CHECK(j.find("\"quality\": \"low\"") != std::string::npos);
}

TEST_CASE("partial json keeps defaults for missing keys") {
  const Settings s = settingsFromJson(R"({"fps":120})");
  CHECK(s.fps == 120);
  CHECK(s.replaySeconds == 60);
}

TEST_CASE("load of missing file returns defaults") {
  const auto dir = freshTempDir("settings_missing");
  CHECK(loadSettings(dir / "nope.json").fps == 60);
}

TEST_CASE("corrupt file returns defaults and is kept as .bak") {
  const auto dir = freshTempDir("settings_corrupt");
  writeFile(dir / "settings.json", "{ not json");
  CHECK(loadSettings(dir / "settings.json").fps == 60);
  CHECK(fs::exists(dir / "settings.json.bak"));
}

TEST_CASE("save then load") {
  const auto dir = freshTempDir("settings_save");
  Settings s;
  s.replaySeconds = 300;
  saveSettings(s, dir / "sub" / "settings.json");
  CHECK(loadSettings(dir / "sub" / "settings.json").replaySeconds == 300);
}

TEST_CASE("sanitize clamps garbage values") {
  Settings s;
  s.fps = 75;
  s.outputHeight = 999;
  s.codec = Codec::AAC;
  s.customKbps = 5;
  s.replaySeconds = 99999;
  s.ramThresholdSeconds = -3;
  s.audioKbps = 77;
  s.container = "avi";
  s.monitorIndex = -1;
  s.audioBlacklist = {"Spotify.EXE"};
  s.nameTemplate = "";
  const Settings r = sanitize(s);
  CHECK(r.fps == 60);
  CHECK(r.outputHeight == 0);
  CHECK(r.codec == Codec::H264);
  CHECK(r.customKbps == 1000);
  CHECK(r.replaySeconds == 1200);
  CHECK(r.ramThresholdSeconds == 0);
  CHECK(r.audioKbps == 160);
  CHECK(r.container == "mp4");
  CHECK(r.monitorIndex == 0);
  CHECK(r.audioBlacklist == std::vector<std::string>{"spotify.exe"});
  CHECK(r.nameTemplate == "{app}_{date}_{time}");
}

TEST_CASE("effectiveVideoKbps uses preset or custom") {
  Settings s;
  s.codec = Codec::H264;
  s.quality = Quality::High;
  CHECK(effectiveVideoKbps(s, 1920, 1080) == 25000);
  s.quality = Quality::Custom;
  s.customKbps = 12345;
  CHECK(effectiveVideoKbps(s, 1920, 1080) == 12345);
}

TEST_CASE("utf8 <-> wide round trip") {
  const std::string u = "Clip è 日本";
  CHECK(wideToUtf8(utf8ToWide(u)) == u);
  CHECK(toLowerAscii("Spotify.EXE") == "spotify.exe");
}
