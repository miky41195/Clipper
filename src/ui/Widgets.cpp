#include "ui/Widgets.h"
#include "ui/Theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace clipper::ui {
using theme::px;

namespace {
struct RowState {
  ImVec2 start;  // cursor pos (window-local)
  float width = 0;
  float labelH = 0;
  float y0Screen = 0;
};
RowState g_row;

ImU32 lighten(ImU32 c, float f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.x += (1 - v.x) * f;
  v.y += (1 - v.y) * f;
  v.z += (1 - v.z) * f;
  return ImGui::ColorConvertFloat4ToU32(v);
}

}  // namespace

float controlWidth() { return px(300); }

void pageHeader(const UiFonts& f, const char* title, const char* desc) {
  ImGui::PushFont(f.title);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (desc) {
    ImGui::PushFont(f.body);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6));
    ImGui::TextUnformatted(desc);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::Dummy({0, px(8)});
}

void beginCard(const char* id) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(20), px(16)});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {px(10), px(10)});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kCard);
  ImGui::PushStyleColor(ImGuiCol_Border, theme::kBorder);
  ImGui::BeginChild(id, {0, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void endCard() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  ImGui::Dummy({0, px(6)});
}

void cardTitle(const UiFonts& f, const char* title, const char* hint) {
  ImGui::PushFont(f.bold);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (hint) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6));
    mutedText(f, hint);
  }
  ImGui::Dummy({0, px(2)});
}

void beginRow(const UiFonts& f, const char* label, const char* hint) {
  ImGui::PushID(label);
  g_row.start = ImGui::GetCursorPos();
  g_row.y0Screen = ImGui::GetCursorScreenPos().y;
  g_row.width = ImGui::GetContentRegionAvail().x;
  const float labelW = g_row.width - controlWidth() - px(24);
  const float frameH0 = ImGui::GetFrameHeight();
  if (!hint) ImGui::SetCursorPosY(g_row.start.y + (frameH0 - ImGui::GetTextLineHeight()) * 0.5f);  // centre single-line label
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(g_row.start.x + labelW);
  ImGui::TextUnformatted(label);
  if (hint) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(7));
    ImGui::PushFont(f.smallText);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
    ImGui::TextWrapped("%s", hint);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  g_row.labelH = ImGui::GetItemRectMax().y - g_row.y0Screen;
  const float frameH = ImGui::GetFrameHeight();
  const float offset = std::max(0.f, (g_row.labelH - frameH) * 0.5f);
  ImGui::SetCursorPos({g_row.start.x + g_row.width - controlWidth(), g_row.start.y + offset});
  ImGui::BeginGroup();
}

void endRow(bool separator) {
  ImGui::EndGroup();
  const float ctrlBottom = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
  const float bottom = std::max(g_row.start.y + g_row.labelH, ctrlBottom);
  const float gap = px(12);
  if (separator) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float y = wp.y + bottom + gap - ImGui::GetScrollY();
    dl->AddLine({wp.x + g_row.start.x, y}, {wp.x + g_row.start.x + g_row.width, y}, theme::kBorder);
  }
  ImGui::SetCursorPos({g_row.start.x, bottom + (separator ? gap * 2 : px(2))});
  ImGui::Dummy({0, 0});
  ImGui::PopID();
}

void alignRight(float itemWidth) {
  ImGui::SetCursorPosX(g_row.start.x + g_row.width - itemWidth);
}

bool toggle(const char* id, bool* v) {
  const float h = px(22), w = px(40);
  alignRight(w);
  const float frameH = ImGui::GetFrameHeight();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (frameH - h) * 0.5f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, {w, h});
  if (clicked) *v = !*v;
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 track = *v ? (hovered ? lighten(theme::accent(), 0.12f) : theme::accent())
                         : (hovered ? theme::kControlHover : theme::kControl);
  dl->AddRectFilled(p, {p.x + w, p.y + h}, track, h * 0.5f);
  const float r = h * 0.5f - px(3);
  const float cx = *v ? p.x + w - h * 0.5f : p.x + h * 0.5f;
  dl->AddCircleFilled({cx, p.y + h * 0.5f}, r, *v ? theme::kBg : theme::kText, 24);
  return clicked;
}

bool segmented(const char* id, const std::vector<std::string>& items, int* index, float width) {
  if (width <= 0) width = controlWidth();
  const float h = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, {p.x + width, p.y + h}, theme::kControl, px(7));
  const float segW = width / float(items.size());
  bool changed = false;
  ImGui::PushID(id);
  for (size_t i = 0; i < items.size(); ++i) {
    const ImVec2 a{p.x + segW * float(i), p.y};
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(int(i));
    if (ImGui::InvisibleButton("seg", {segW, h})) {
      changed = *index != int(i);
      *index = int(i);
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const bool sel = *index == int(i);
    const float in = px(3);
    if (sel) dl->AddRectFilled({a.x + in, a.y + in}, {a.x + segW - in, a.y + h - in}, theme::accent(), px(5));
    else if (hovered) dl->AddRectFilled({a.x + in, a.y + in}, {a.x + segW - in, a.y + h - in}, theme::kControlHover, px(5));
    const ImVec2 ts = ImGui::CalcTextSize(items[i].c_str());
    dl->AddText({a.x + (segW - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f}, sel ? theme::kBg : (hovered ? theme::kText : theme::kMuted),
                items[i].c_str());
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy({width, h});
  return changed;
}

bool combo(const char* id, const std::vector<std::string>& items, int* index, float width) {
  const float w = width > 0 ? width : controlWidth();
  ImGui::SetNextItemWidth(w);
  const char* preview = (*index >= 0 && *index < int(items.size())) ? items[size_t(*index)].c_str() : "";
  bool changed = false;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(6), px(6)});
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge | ImGuiComboFlags_NoArrowButton);
  {
    // Chevron instead of ImGui's filled triangle.
    const float h = ImGui::GetFrameHeight(), s = px(4);
    const ImVec2 c{p.x + w - px(18), p.y + h * 0.5f + px(1)};
    ImGui::GetWindowDrawList()->AddPolyline(std::array<ImVec2, 3>{ImVec2{c.x - s, c.y - s * 0.5f}, ImVec2{c.x, c.y + s * 0.5f},
                                                                   ImVec2{c.x + s, c.y - s * 0.5f}}
                                                .data(),
                                            3, theme::kMuted, 0, px(1.5f));
  }
  if (open) {
    for (size_t i = 0; i < items.size(); ++i) {
      const bool sel = *index == int(i);
      if (ImGui::Selectable(items[i].c_str(), sel, 0, {0, ImGui::GetFrameHeight()})) {
        changed = *index != int(i);
        *index = int(i);
      }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopStyleVar();
  return changed;
}

bool stepSlider(const char* id, const std::vector<int>& steps, int* value, const std::string& label, float width) {
  int idx = 0;
  for (size_t i = 0; i < steps.size(); ++i)
    if (steps[i] <= *value) idx = int(i);
  const float w = width > 0 ? width : controlWidth();
  const float h = ImGui::GetFrameHeight();
  const float labelW = px(96);
  const float trackW = w - labelW;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, {trackW, h});
  const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
  const float knobR = px(8);
  const float x0 = p.x + knobR, x1 = p.x + trackW - knobR;
  const int last = int(steps.size()) - 1;
  bool changed = false;
  if (active && last > 0) {
    const float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.f, 1.f);
    const int ni = int(std::lround(t * float(last)));
    if (ni != idx) {
      idx = ni;
      *value = steps[size_t(idx)];
      changed = true;
    }
  }
  const float kx = last > 0 ? x0 + (x1 - x0) * float(idx) / float(last) : x0;
  const float cy = p.y + h * 0.5f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled({x0, cy - px(2)}, {x1, cy + px(2)}, theme::kControlHover, px(2));
  dl->AddRectFilled({x0, cy - px(2)}, {kx, cy + px(2)}, theme::accent(), px(2));
  for (int i = 0; i <= last && last <= 16; ++i) {  // step ticks
    const float tx = x0 + (x1 - x0) * float(i) / float(std::max(1, last));
    if (tx > kx + knobR) dl->AddCircleFilled({tx, cy}, px(1.5f), theme::kFaint, 8);
  }
  dl->AddCircleFilled({kx, cy}, knobR + (active || hovered ? px(1.5f) : 0), theme::kText, 24);
  dl->AddCircleFilled({kx, cy}, px(3), theme::accent(), 16);
  const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
  dl->AddText({p.x + w - ts.x, cy - ts.y * 0.5f}, theme::kText, label.c_str());
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy({w, h});
  return changed;
}

static bool styledButton(const char* label, bool enabled, float width, ImU32 bg, ImU32 text) {
  ImGui::BeginDisabled(!enabled);
  ImGui::PushStyleColor(ImGuiCol_Button, bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, lighten(bg, 0.1f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, lighten(bg, 0.2f));
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {px(16), px(8)});
  const bool r = ImGui::Button(label, {width, 0});
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  ImGui::EndDisabled();
  return r;
}

bool primaryButton(const char* label, bool enabled, float width) {
  return styledButton(label, enabled, width, theme::accent(), theme::kBg);
}

bool secondaryButton(const char* label, bool enabled, float width) {
  return styledButton(label, enabled, width, theme::kControl, theme::kText);
}

bool iconButton(const UiFonts& f, const char* id, const char* glyph, const char* tooltip) {
  const float s = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, {s, s});
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (hovered) dl->AddRectFilled(p, {p.x + s, p.y + s}, theme::kControlHover, px(6));
  ImGui::PushFont(f.icon);
  const ImVec2 ts = ImGui::CalcTextSize(glyph);
  dl->AddText({p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f}, hovered ? theme::kText : theme::kMuted, glyph);
  ImGui::PopFont();
  if (tooltip && hovered) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

bool navItem(const UiFonts& f, const char* glyph, const char* label, bool selected) {
  const float w = ImGui::GetContentRegionAvail().x, h = px(36);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::PushID(label);
  const bool clicked = ImGui::InvisibleButton("nav", {w, h});
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected) {
    dl->AddRectFilled(p, {p.x + w, p.y + h}, theme::accentAlpha(30), px(8));
    dl->AddRectFilled({p.x, p.y + px(9)}, {p.x + px(3), p.y + h - px(9)}, theme::accent(), px(2));
  } else if (hovered) {
    dl->AddRectFilled(p, {p.x + w, p.y + h}, IM_COL32(255, 255, 255, 10), px(8));
  }
  const ImU32 col = selected ? theme::kText : (hovered ? theme::kText : theme::kMuted);
  ImGui::PushFont(f.icon);
  const ImVec2 is = ImGui::CalcTextSize(glyph);
  dl->AddText({p.x + px(14), p.y + (h - is.y) * 0.5f}, selected ? theme::accent() : col, glyph);
  ImGui::PopFont();
  ImGui::PushFont(selected ? f.bold : f.body);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText({p.x + px(44), p.y + (h - ts.y) * 0.5f}, col, label);
  ImGui::PopFont();
  ImGui::Dummy({0, px(2)});
  return clicked;
}

float drawKeycaps(const UiFonts& f, const std::string& combo, ImVec2 p, float h) {
  ImGui::PushFont(f.smallText);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  size_t pos = 0;
  float x = p.x;
  while (pos <= combo.size()) {
    const size_t plus = combo.find('+', pos);
    const std::string part = combo.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
    const ImVec2 ts = ImGui::CalcTextSize(part.c_str());
    const float w = ts.x + px(16);
    const float capH = h - px(10);
    const float y = p.y + px(5);
    dl->AddRectFilled({x, y}, {x + w, y + capH}, IM_COL32(52, 58, 70, 255), px(5));
    dl->AddRectFilled({x, y + capH - px(2)}, {x + w, y + capH}, IM_COL32(0, 0, 0, 60), px(5), ImDrawFlags_RoundCornersBottom);
    dl->AddText({x + px(8), y + (capH - ts.y) * 0.5f - px(1)}, theme::kText, part.c_str());
    x += w + px(5);
    if (plus == std::string::npos) break;
    pos = plus + 1;
  }
  ImGui::PopFont();
  return x - p.x;
}

void keycaps(const UiFonts& f, const std::string& combo) {
  const float h = ImGui::GetFrameHeight();
  const float w = drawKeycaps(f, combo, ImGui::GetCursorScreenPos(), h);
  ImGui::Dummy({w, h});
}

bool colorSwatch(const char* id, ImU32 color, bool selected) {
  const float s = px(28);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, {s, s});
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c{p.x + s * 0.5f, p.y + s * 0.5f};
  if (selected) dl->AddCircle(c, s * 0.5f, theme::kText, 32, px(2));
  dl->AddCircleFilled(c, s * 0.5f - px(selected ? 4 : (hovered ? 2 : 3)), color, 32);
  return clicked;
}

void statusPill(const UiFonts& f, ImU32 dot, const std::string& text) {
  ImGui::PushFont(f.smallText);
  const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = px(26), w = ts.x + px(30);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, {p.x + w, p.y + h}, IM_COL32(255, 255, 255, 10), h * 0.5f);
  dl->AddCircleFilled({p.x + px(13), p.y + h * 0.5f}, px(4), dot, 16);
  dl->AddText({p.x + px(22), p.y + (h - ts.y) * 0.5f}, theme::kMuted, text.c_str());
  ImGui::Dummy({w, h});
  ImGui::PopFont();
}

void mutedText(const UiFonts& f, const std::string& text, bool wrap) {
  ImGui::PushFont(f.smallText);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::kMuted);
  if (wrap) ImGui::TextWrapped("%s", text.c_str());
  else ImGui::TextUnformatted(text.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

}  // namespace clipper::ui
