#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace clipper {

// "#RRGGBB" <-> ImU32 (0xAABBGGRR, opaque).
std::optional<uint32_t> parseHexColor(std::string_view text);
std::string toHexColor(uint32_t imu32);

}  // namespace clipper
