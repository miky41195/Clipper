#include "ui/Fonts.h"
#include <filesystem>

namespace clipper {

UiFonts loadFonts(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  static const ImWchar kText[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x2027, 0x2190, 0x2193, 0x2248, 0x2248, 0};
  static const ImWchar kIcons[] = {0xE700, 0xF8FF, 0};
  const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* semibold = "C:\\Windows\\Fonts\\seguisb.ttf";
  const char* icons = std::filesystem::exists("C:\\Windows\\Fonts\\SegoeIcons.ttf") ? "C:\\Windows\\Fonts\\SegoeIcons.ttf"
                                                                                      : "C:\\Windows\\Fonts\\segmdl2.ttf";
  auto add = [&](const char* file, float size) {
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    ImFont* f = io.Fonts->AddFontFromFileTTF(file, size * scale, &cfg, kText);
    ImFontConfig ic;
    ic.MergeMode = true;
    ic.PixelSnapH = true;
    ic.GlyphOffset = {0, size * scale * 0.18f};
    ic.GlyphMinAdvanceX = size * scale;
    io.Fonts->AddFontFromFileTTF(icons, size * scale * 0.95f, &ic, kIcons);
    return f;
  };
  UiFonts f;
  f.body = add(regular, 15);
  f.small = add(regular, 13);
  f.bold = add(semibold, 15);
  f.title = add(semibold, 22);
  ImFontConfig ic;
  ic.PixelSnapH = true;
  f.icon = io.Fonts->AddFontFromFileTTF(icons, 18 * scale, &ic, kIcons);
  io.FontDefault = f.body;
  return f;
}

}  // namespace clipper
