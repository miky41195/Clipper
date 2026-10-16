#pragma once
#include "audio/ProcessTree.h"
#include "capture/Monitors.h"
#include "core/Settings.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clipper {

enum class AppState { Buffering, Recording, Paused, Off };

// Everything the UI needs from the rest of the app, as callbacks so pages stay testable.
struct UiContext {
  std::function<Settings()> currentSettings;
  std::function<std::string(const Settings&)> applySettings;  // "" = ok, else an error to show
  std::function<std::vector<MonitorInfo>()> monitors;
  std::function<std::vector<std::pair<std::string, std::string>>()> microphones;  // {id, name}
  std::function<std::vector<AppGroup>()> liveAudioApps;
  std::function<std::string(const std::string& exe)> appName;
  std::function<void(bool)> suspendHotkeys;  // while a hotkey field is capturing
  std::function<std::pair<AppState, std::string>()> status;
  std::function<std::filesystem::path(const Settings&)> clipsFolder;
  std::function<std::filesystem::path(const Settings&)> bufferFolder;
  std::function<void(const std::filesystem::path&)> openFolder;
  std::function<std::optional<std::filesystem::path>(const std::filesystem::path& initial)> pickFolder;
  std::function<bool()> autostart;
  std::function<void(bool)> setAutostart;
};

}  // namespace clipper
