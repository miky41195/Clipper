#include "core/Hotkey.h"
#include <cctype>
#include <charconv>

namespace clipper {
namespace {

struct NamedKey {
  std::string_view name;
  unsigned vk;
};

constexpr NamedKey kNamed[] = {
    {"Space", 0x20}, {"Enter", 0x0D}, {"Tab", 0x09},    {"Insert", 0x2D},      {"Delete", 0x2E},
    {"Home", 0x24},  {"End", 0x23},   {"PageUp", 0x21}, {"PageDown", 0x22},    {"Left", 0x25},
    {"Up", 0x26},    {"Right", 0x27}, {"Down", 0x28},   {"PrintScreen", 0x2C}, {"Pause", 0x13},
};

bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

std::optional<unsigned> keyFromName(std::string_view n) {
  if (n.size() == 1) {
    const char c = char(std::toupper(static_cast<unsigned char>(n[0])));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return unsigned(c);
    return std::nullopt;
  }
  if ((n[0] == 'F' || n[0] == 'f') && n.size() <= 3) {
    int k = 0;
    const auto r = std::from_chars(n.data() + 1, n.data() + n.size(), k);
    if (r.ec == std::errc{} && r.ptr == n.data() + n.size() && k >= 1 && k <= 24) return 0x6Fu + unsigned(k);
    return std::nullopt;
  }
  if (n.size() == 7 && ieq(n.substr(0, 6), "Numpad") && n[6] >= '0' && n[6] <= '9') return 0x60u + unsigned(n[6] - '0');
  for (const auto& k : kNamed)
    if (ieq(k.name, n)) return k.vk;
  return std::nullopt;
}

std::string nameFromKey(unsigned vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, char(vk));
  if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);
  if (vk >= 0x60 && vk <= 0x69) return "Numpad" + std::to_string(vk - 0x60);
  for (const auto& k : kNamed)
    if (k.vk == vk) return std::string(k.name);
  return "?";
}

}  // namespace

std::optional<Hotkey> parseHotkey(std::string_view text) {
  Hotkey hk;
  bool haveKey = false;
  size_t pos = 0;
  while (true) {
    const size_t plus = text.find('+', pos);
    const std::string_view tok =
        trim(text.substr(pos, plus == std::string_view::npos ? std::string_view::npos : plus - pos));
    if (tok.empty()) return std::nullopt;
    if (ieq(tok, "Ctrl") || ieq(tok, "Control")) hk.mods |= kModCtrl;
    else if (ieq(tok, "Alt")) hk.mods |= kModAlt;
    else if (ieq(tok, "Shift")) hk.mods |= kModShift;
    else if (ieq(tok, "Win")) hk.mods |= kModWin;
    else {
      if (haveKey) return std::nullopt;
      const auto vk = keyFromName(tok);
      if (!vk) return std::nullopt;
      hk.vk = *vk;
      haveKey = true;
    }
    if (plus == std::string_view::npos) break;
    pos = plus + 1;
  }
  if (!haveKey) return std::nullopt;
  const bool isFunctionKey = hk.vk >= 0x70 && hk.vk <= 0x87;
  if (hk.mods == 0 && !isFunctionKey) return std::nullopt;
  return hk;
}

std::string formatHotkey(const Hotkey& hk) {
  std::string out;
  if (hk.mods & kModCtrl) out += "Ctrl+";
  if (hk.mods & kModAlt) out += "Alt+";
  if (hk.mods & kModShift) out += "Shift+";
  if (hk.mods & kModWin) out += "Win+";
  return out + nameFromKey(hk.vk);
}

}  // namespace clipper
