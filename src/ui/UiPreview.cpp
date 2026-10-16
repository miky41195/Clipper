#include "ui/UiPreview.h"
#include "ui/AppUi.h"
#include "ui/Color.h"
#include "ui/I18n.h"
#include "ui/UiWindow.h"
#include <map>

namespace clipper {

UiContext previewContext() {
  UiContext c;
  c.currentSettings = [] { return Settings{}; };
  c.applySettings = [](const Settings&) { return std::string{}; };
  c.monitors = [] {
    std::vector<MonitorInfo> m(2);
    m[0] = {0, nullptr, L"\\\\.\\DISPLAY1", 2560, 1440, true};
    m[1] = {1, nullptr, L"\\\\.\\DISPLAY2", 1920, 1080, false};
    return m;
  };
  c.microphones = [] {
    return std::vector<std::pair<std::string, std::string>>{{"id1", "Microfono (HyperX Cloud II)"}, {"id2", "Microfono (Webcam)"}};
  };
  c.liveAudioApps = [] {
    return std::vector<AppGroup>{{"thebazaar.exe", {1}}, {"discord.exe", {2}}, {"spotify.exe", {3}}};
  };
  c.appName = [](const std::string& exe) {
    static const std::map<std::string, std::string> names{{"thebazaar.exe", "The Bazaar"}, {"discord.exe", "Discord"}, {"spotify.exe", "Spotify"}};
    const auto it = names.find(exe);
    return it == names.end() ? exe : it->second;
  };
  c.suspendHotkeys = [](bool) {};
  c.status = [] { return std::pair{AppState::Buffering, trf("status.buffering", std::string("1 min"))}; };
  c.clipsFolder = [](const Settings&) { return std::filesystem::path(L"C:\\Users\\Player\\Videos\\Clipper"); };
  c.bufferFolder = [](const Settings&) { return std::filesystem::path(L"C:\\Users\\Player\\AppData\\Local\\Clipper\\buffer"); };
  c.openFolder = [](const std::filesystem::path&) {};
  c.pickFolder = [](const std::filesystem::path&) { return std::optional<std::filesystem::path>{}; };
  c.autostart = [] { return false; };
  c.setAutostart = [](bool) {};
  return c;
}

bool renderPreview(const std::string& pageSpec, const std::filesystem::path& png, float scale) {
  std::string page = pageSpec;
  ImVec2 mouse{-1, -1};
  if (const auto at = page.find('@'); at != std::string::npos) {
    const auto comma = page.find(',', at);
    mouse = {std::stof(page.substr(at + 1, comma - at - 1)) * scale, std::stof(page.substr(comma + 1)) * scale};
    page.resize(at);
  }
  UiContext ctx = previewContext();
  AppUi app(ctx);
  static const std::map<std::string, SettingsSection> sections{
      {"settings.capture", SettingsSection::Capture}, {"settings.replay", SettingsSection::Replay},
      {"settings.audio", SettingsSection::Audio},     {"settings.hotkeys", SettingsSection::Hotkeys},
      {"settings.output", SettingsSection::Output},   {"settings.general", SettingsSection::General}};
  if (const auto it = sections.find(page); it != sections.end()) app.showSettings(it->second);
  else app.showGallery();
  const ImU32 accent = parseHexColor(Settings{}.accent).value_or(0xFF84DC3D);
  return renderOffscreen(int(1120 * scale), int(740 * scale), scale, accent, [&](const UiFonts& f) { app.draw(f); }, png, mouse);
}

}  // namespace clipper
