#pragma once
#include "ui/Fonts.h"
#include "ui/SettingsModel.h"
#include "ui/UiContext.h"
#include <string>
#include <vector>

namespace clipper {

enum class SettingsSection { Capture, Replay, Audio, Hotkeys, Output, General };
inline constexpr int kSettingsSectionCount = 6;

class SettingsPage {
public:
  explicit SettingsPage(UiContext& ctx);

  void onShow();  // reload applied settings and device/app lists
  void draw(const UiFonts& f, SettingsSection section);

  SettingsForm& form() { return form_; }
  bool dirty() const;
  bool applyDraft();  // true on success; on failure lastError() explains why
  const std::string& lastError() const { return error_; }

private:
  void refreshLists();
  void drawCapture(const UiFonts& f);
  void drawReplay(const UiFonts& f);
  void drawAudio(const UiFonts& f);
  void drawHotkeys(const UiFonts& f);
  void drawOutput(const UiFonts& f);
  void drawGeneral(const UiFonts& f);
  void drawEstimate(const UiFonts& f);
  void drawFooter(const UiFonts& f, float height);
  void hotkeyField(const UiFonts& f, int slot, std::string* value);
  void stopCapture();
  std::pair<int, int> captureSize() const;
  int audioTrackCount() const;

  UiContext& ctx_;
  SettingsForm form_;
  bool autostartSaved_ = false;
  bool autostartDraft_ = false;
  std::string error_;
  std::string message_;
  std::vector<MonitorInfo> monitors_;
  std::vector<std::pair<std::string, std::string>> mics_;
  std::vector<AppGroup> apps_;
  int capturing_ = -1;
  std::string hotkeyHint_;
};

}  // namespace clipper
