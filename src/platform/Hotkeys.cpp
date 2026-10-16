#include "platform/Hotkeys.h"
#include "core/I18n.h"
#include "core/Hotkey.h"
#include <format>

namespace clipper {

HotkeyManager::HotkeyManager(HWND hwnd) : hwnd_(hwnd) {}

HotkeyManager::~HotkeyManager() { unregisterAll(); }

void HotkeyManager::unregisterAll() {
  for (int id = 1; id <= 3; ++id) UnregisterHotKey(hwnd_, id);
}

std::map<Action, std::string> HotkeyManager::apply(const HotkeySettings& s) {
  unregisterAll();
  std::map<Action, std::string> errors;
  const std::pair<Action, const std::string*> items[] = {
      {Action::SaveClip, &s.saveClip}, {Action::ToggleRecord, &s.toggleRecord}, {Action::OpenGallery, &s.openGallery}};
  for (const auto& [action, text] : items) {
    if (text->empty()) continue;  // disabled
    const auto hk = parseHotkey(*text);
    if (!hk) {
      errors[action] = trf("hk.err.invalid", *text);
      continue;
    }
    if (!RegisterHotKey(hwnd_, int(action), hk->mods | MOD_NOREPEAT, hk->vk))
      errors[action] = trf("hk.err.taken", formatHotkey(*hk));
  }
  return errors;
}

std::optional<Action> HotkeyManager::fromId(WPARAM id) {
  if (id >= 1 && id <= 3) return Action(id);
  return std::nullopt;
}

}  // namespace clipper
