#include "ui/HotkeyCapture.h"
#include "core/Hotkey.h"

namespace clipper {

std::optional<std::string> hotkeyFromKeys(bool ctrl, bool alt, bool shift, bool win, unsigned vk) {
  const Hotkey hk{(ctrl ? kModCtrl : 0u) | (alt ? kModAlt : 0u) | (shift ? kModShift : 0u) | (win ? kModWin : 0u), vk};
  const std::string text = formatHotkey(hk);
  if (text.ends_with("?")) return std::nullopt;  // key without a name (modifier itself, OEM key…)
  if (!parseHotkey(text)) return std::nullopt;
  return text;
}

}  // namespace clipper
