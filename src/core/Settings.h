#pragma once
#include "core/Media.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace clipper {

struct HotkeySettings {
  std::string saveClip = "Alt+F10";
  std::string toggleRecord = "Alt+Shift+F9";  // Alt+F9 is taken by NVIDIA's overlay
  std::string openGallery = "Alt+G";
};

struct Settings {
  // Capture
  int monitorIndex = 0;
  int outputHeight = 0;  // 0 = native; else 720/1080/1440/2160
  int fps = 60;
  Codec codec = Codec::AV1;
  Quality quality = Quality::High;
  int customKbps = 25000;
  bool captureCursor = true;
  // Replay
  int replaySeconds = 60;
  int ramThresholdSeconds = 120;
  std::string tempDir;  // empty = %LOCALAPPDATA%\Clipper\buffer
  // Audio
  std::vector<std::string> audioBlacklist;  // lowercase exe names
  std::string micDeviceId;                  // empty = default capture device
  bool captureMic = true;
  bool mixTrack = true;
  int audioKbps = 160;
  // Hotkeys
  HotkeySettings hotkeys;
  // Output
  std::string outputDir;  // empty = <Videos>\Clipper
  std::string container = "mp4";
  std::string nameTemplate = "{app}_{date}_{time}";
  // General
  bool sounds = true;
  std::string toastCorner = "top-right";
  std::string language = "it";
  std::string accent = "#3DDC84";
};

Settings sanitize(Settings s);
std::string settingsToJson(const Settings& s);
Settings settingsFromJson(std::string_view json);
Settings loadSettings(const std::filesystem::path& file);
void saveSettings(const Settings& s, const std::filesystem::path& file);
int effectiveVideoKbps(const Settings& s, int width, int height);

}  // namespace clipper
