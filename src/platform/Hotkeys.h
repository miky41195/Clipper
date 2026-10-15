#pragma once
#include "core/Settings.h"
#include <windows.h>
#include <map>
#include <optional>
#include <string>

namespace clipper {

enum class Action { SaveClip = 1, ToggleRecord = 2, OpenGallery = 3 };

// Global hotkeys delivered as WM_HOTKEY (wParam = Action id) to the given window.
class HotkeyManager {
public:
  explicit HotkeyManager(HWND hwnd);
  ~HotkeyManager();
  HotkeyManager(const HotkeyManager&) = delete;
  HotkeyManager& operator=(const HotkeyManager&) = delete;

  // Re-registers every hotkey. Returns an Italian error message per action that failed.
  std::map<Action, std::string> apply(const HotkeySettings& hk);
  static std::optional<Action> fromId(WPARAM id);

private:
  void unregisterAll();
  HWND hwnd_;
};

}  // namespace clipper
