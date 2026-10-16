#pragma once
#include <imgui.h>

namespace clipper::theme {

constexpr ImU32 kBg = IM_COL32(15, 17, 21, 255);
constexpr ImU32 kPanel = IM_COL32(22, 24, 29, 255);
constexpr ImU32 kCard = IM_COL32(28, 31, 38, 255);
constexpr ImU32 kCardHover = IM_COL32(34, 38, 46, 255);
constexpr ImU32 kControl = IM_COL32(38, 42, 51, 255);
constexpr ImU32 kControlHover = IM_COL32(46, 51, 61, 255);
constexpr ImU32 kText = IM_COL32(242, 243, 245, 255);
constexpr ImU32 kMuted = IM_COL32(154, 160, 171, 255);
constexpr ImU32 kFaint = IM_COL32(107, 114, 128, 255);
constexpr ImU32 kBorder = IM_COL32(255, 255, 255, 16);
constexpr ImU32 kDanger = IM_COL32(255, 92, 92, 255);
constexpr ImU32 kWarning = IM_COL32(255, 176, 32, 255);

void apply(ImU32 accent, float scale);
ImU32 accent();
ImU32 accentAlpha(int alpha);  // accent with a different alpha (0..255)
float scale();
inline float px(float v) { return v * scale(); }

}  // namespace clipper::theme
