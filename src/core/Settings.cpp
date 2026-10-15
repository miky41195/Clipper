#include "core/Settings.h"
#include "core/SizeEstimate.h"
#include "core/Text.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace clipper {

NLOHMANN_JSON_SERIALIZE_ENUM(Codec, {{Codec::H264, "h264"}, {Codec::HEVC, "hevc"}, {Codec::AV1, "av1"}, {Codec::AAC, "aac"}})
NLOHMANN_JSON_SERIALIZE_ENUM(Quality, {{Quality::Low, "low"}, {Quality::Medium, "medium"}, {Quality::High, "high"}, {Quality::Custom, "custom"}})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HotkeySettings, saveClip, toggleRecord, openGallery)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, monitorIndex, outputHeight, fps, codec, quality, customKbps,
                                                captureCursor, replaySeconds, ramThresholdSeconds, tempDir, audioBlacklist,
                                                micDeviceId, captureMic, mixTrack, audioKbps, hotkeys, outputDir, container,
                                                nameTemplate, sounds, toastCorner, language)

namespace fs = std::filesystem;

template <class T, size_t N>
static bool oneOf(const T (&values)[N], const T& v) {
  return std::find(std::begin(values), std::end(values), v) != std::end(values);
}

Settings sanitize(Settings s) {
  static constexpr int kFps[] = {30, 60, 120, 144};
  static constexpr int kHeights[] = {0, 720, 1080, 1440, 2160};
  static constexpr int kAudio[] = {96, 128, 160, 192};
  if (!oneOf(kFps, s.fps)) s.fps = 60;
  if (!oneOf(kHeights, s.outputHeight)) s.outputHeight = 0;
  if (s.codec == Codec::AAC) s.codec = Codec::H264;
  s.customKbps = std::clamp(s.customKbps, 1000, 150000);
  s.replaySeconds = std::clamp(s.replaySeconds, 10, 1200);
  s.ramThresholdSeconds = std::clamp(s.ramThresholdSeconds, 0, 1200);
  if (!oneOf(kAudio, s.audioKbps)) s.audioKbps = 160;
  if (s.container != "mp4" && s.container != "mkv") s.container = "mp4";
  if (s.monitorIndex < 0) s.monitorIndex = 0;
  for (auto& e : s.audioBlacklist) e = toLowerAscii(e);
  if (s.nameTemplate.empty()) s.nameTemplate = "{app}_{date}_{time}";
  return s;
}

std::string settingsToJson(const Settings& s) { return nlohmann::json(s).dump(2); }

Settings settingsFromJson(std::string_view json) { return nlohmann::json::parse(json).get<Settings>(); }

Settings loadSettings(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return Settings{};
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  try {
    return sanitize(settingsFromJson(text));
  } catch (const std::exception&) {
    auto bak = file;
    bak += L".bak";
    std::error_code ec;
    fs::rename(file, bak, ec);
    return Settings{};
  }
}

void saveSettings(const Settings& s, const fs::path& file) {
  if (file.has_parent_path()) fs::create_directories(file.parent_path());
  auto tmp = file;
  tmp += L".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << settingsToJson(s);
  }
  fs::rename(tmp, file);
}

int effectiveVideoKbps(const Settings& s, int width, int height) {
  if (s.quality == Quality::Custom) return s.customKbps;
  return presetVideoKbps(s.quality, s.codec, width, height, s.fps);
}

}  // namespace clipper
