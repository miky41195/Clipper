#pragma once
#include <imgui.h>

namespace clipper {

// Segoe UI (Windows system font) with Segoe Fluent Icons merged in, so icon code points
// (see ui/Icons.h) can be mixed with text.
struct UiFonts {
  ImFont* body = nullptr;   // 15 px
  ImFont* smallText = nullptr;  // 13 px
  ImFont* bold = nullptr;   // 15 px semibold
  ImFont* title = nullptr;  // 22 px semibold
  ImFont* icon = nullptr;   // 18 px icons only
};

UiFonts loadFonts(float scale);  // replaces the current context's font atlas

}  // namespace clipper
