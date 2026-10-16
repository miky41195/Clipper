#include "ui/Color.h"
#include <charconv>
#include <format>

namespace clipper {

std::optional<uint32_t> parseHexColor(std::string_view t) {
  if (!t.empty() && t.front() == '#') t.remove_prefix(1);
  if (t.size() != 6) return std::nullopt;
  uint32_t rgb = 0;
  const auto r = std::from_chars(t.data(), t.data() + t.size(), rgb, 16);
  if (r.ec != std::errc{} || r.ptr != t.data() + t.size()) return std::nullopt;
  const uint32_t red = (rgb >> 16) & 0xFF, green = (rgb >> 8) & 0xFF, blue = rgb & 0xFF;
  return 0xFF000000u | (blue << 16) | (green << 8) | red;
}

std::string toHexColor(uint32_t c) { return std::format("#{:02X}{:02X}{:02X}", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF); }

}  // namespace clipper
