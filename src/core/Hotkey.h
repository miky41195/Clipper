#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace clipper {

constexpr unsigned kModAlt = 0x1;
constexpr unsigned kModCtrl = 0x2;
constexpr unsigned kModShift = 0x4;
constexpr unsigned kModWin = 0x8;

struct Hotkey {
  unsigned mods = 0;
  unsigned vk = 0;
  bool operator==(const Hotkey&) const = default;
};

std::optional<Hotkey> parseHotkey(std::string_view text);
std::string formatHotkey(const Hotkey& hk);

}  // namespace clipper
