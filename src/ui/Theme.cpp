#include "ui/Theme.h"

namespace clipper::theme {
namespace {
ImU32 g_accent = IM_COL32(61, 220, 132, 255);
float g_scale = 1.f;

ImVec4 v(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
}  // namespace

ImU32 accent() { return g_accent; }
float scale() { return g_scale; }

ImU32 accentAlpha(int alpha) { return (g_accent & 0x00FFFFFF) | (ImU32(alpha & 0xFF) << 24); }

void apply(ImU32 accentColor, float s) {
  g_accent = accentColor | 0xFF000000;
  g_scale = s;
  ImGuiStyle& st = ImGui::GetStyle();
  st = ImGuiStyle();
  st.WindowPadding = {0, 0};
  st.FramePadding = {10, 7};
  st.ItemSpacing = {10, 10};
  st.ItemInnerSpacing = {8, 6};
  st.CellPadding = {8, 6};
  st.ScrollbarSize = 10;
  st.GrabMinSize = 14;
  st.WindowRounding = 0;
  st.ChildRounding = 10;
  st.FrameRounding = 6;
  st.PopupRounding = 8;
  st.ScrollbarRounding = 6;
  st.GrabRounding = 7;
  st.TabRounding = 6;
  st.WindowBorderSize = 0;
  st.ChildBorderSize = 1;
  st.FrameBorderSize = 0;
  st.PopupBorderSize = 1;
  st.SelectableTextAlign = {0, 0.5f};
  st.DisabledAlpha = 0.45f;

  ImVec4* c = st.Colors;
  c[ImGuiCol_Text] = v(kText);
  c[ImGuiCol_TextDisabled] = v(kFaint);
  c[ImGuiCol_WindowBg] = v(kBg);
  c[ImGuiCol_ChildBg] = v(kCard);
  c[ImGuiCol_PopupBg] = v(IM_COL32(30, 33, 41, 252));
  c[ImGuiCol_Border] = v(kBorder);
  c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  c[ImGuiCol_FrameBg] = v(kControl);
  c[ImGuiCol_FrameBgHovered] = v(kControlHover);
  c[ImGuiCol_FrameBgActive] = v(IM_COL32(52, 58, 70, 255));
  c[ImGuiCol_TitleBg] = v(kPanel);
  c[ImGuiCol_TitleBgActive] = v(kPanel);
  c[ImGuiCol_MenuBarBg] = v(kPanel);
  c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
  c[ImGuiCol_ScrollbarGrab] = v(IM_COL32(255, 255, 255, 28));
  c[ImGuiCol_ScrollbarGrabHovered] = v(IM_COL32(255, 255, 255, 48));
  c[ImGuiCol_ScrollbarGrabActive] = v(IM_COL32(255, 255, 255, 64));
  c[ImGuiCol_CheckMark] = v(g_accent);
  c[ImGuiCol_SliderGrab] = v(g_accent);
  c[ImGuiCol_SliderGrabActive] = v(g_accent);
  c[ImGuiCol_Button] = v(kControl);
  c[ImGuiCol_ButtonHovered] = v(kControlHover);
  c[ImGuiCol_ButtonActive] = v(IM_COL32(52, 58, 70, 255));
  c[ImGuiCol_Header] = v(accentAlpha(40));
  c[ImGuiCol_HeaderHovered] = v(IM_COL32(255, 255, 255, 14));
  c[ImGuiCol_HeaderActive] = v(accentAlpha(60));
  c[ImGuiCol_Separator] = v(kBorder);
  c[ImGuiCol_SeparatorHovered] = v(accentAlpha(120));
  c[ImGuiCol_SeparatorActive] = v(g_accent);
  c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
  c[ImGuiCol_ResizeGripHovered] = {0, 0, 0, 0};
  c[ImGuiCol_ResizeGripActive] = {0, 0, 0, 0};
  c[ImGuiCol_TextSelectedBg] = v(accentAlpha(90));
  c[ImGuiCol_NavCursor] = v(g_accent);
  c[ImGuiCol_ModalWindowDimBg] = v(IM_COL32(0, 0, 0, 140));
  st.ScaleAllSizes(s);
}

}  // namespace clipper::theme
