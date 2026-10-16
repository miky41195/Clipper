#pragma once
#include <optional>
#include <string>

namespace clipper {

// Canonical hotkey text for the pressed modifiers + key, or nullopt if the combination is not usable.
std::optional<std::string> hotkeyFromKeys(bool ctrl, bool alt, bool shift, bool win, unsigned vk);

}  // namespace clipper
