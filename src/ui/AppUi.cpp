#include "ui/AppUi.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include <imgui.h>

namespace clipper {
using theme::px;

AppUi::AppUi(UiContext& ctx) : ctx_(ctx), settings_(ctx), gallery_(ctx) {}
AppUi::~AppUi() = default;

void AppUi::showGallery() {
  if (page_ != Page::Gallery) gallery_.refresh();
  page_ = Page::Gallery;
}

void AppUi::showSettings(SettingsSection s) {
  page_ = Page::Settings;
  section_ = s;
}

void AppUi::onOpen() {
  settings_.onShow();
  gallery_.onOpen();
}

void AppUi::onClose() { gallery_.onClose(); }

void AppUi::clipsChanged() { gallery_.refresh(); }

bool AppUi::wantsAnimation() const { return page_ == Page::Gallery && gallery_.wantsAnimation(); }

void AppUi::draw(const UiFonts& f) {
  drawSidebar(f);
  ImGui::SameLine(0, 0);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kBg);
  ImGui::BeginChild("##page", {0, 0}, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  if (page_ == Page::Settings) settings_.draw(f, section_);
  else gallery_.draw(f);
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void AppUi::drawSidebar(const UiFonts& f) {
  const float w = px(236);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kPanel);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {px(14), px(20)});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {px(8), px(2)});
  ImGui::BeginChild("##sidebar", {w, 0}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 wp = ImGui::GetWindowPos();
  dl->AddLine({wp.x + w - 1, wp.y}, {wp.x + w - 1, wp.y + ImGui::GetWindowHeight()}, theme::kBorder);

  // Logo
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float logo = px(34);
  dl->AddRectFilled({p.x + px(4), p.y}, {p.x + px(4) + logo, p.y + logo}, theme::accent(), px(9));
  ImGui::PushFont(f.icon);
  const ImVec2 is = ImGui::CalcTextSize(icon::kVideo);
  dl->AddText({p.x + px(4) + (logo - is.x) * 0.5f, p.y + (logo - is.y) * 0.5f}, theme::kBg, icon::kVideo);
  ImGui::PopFont();
  ImGui::PushFont(f.title);
  const float th = ImGui::GetTextLineHeight();
  dl->AddText({p.x + px(4) + logo + px(12), p.y + (logo - th) * 0.5f}, theme::kText, "Clipper");
  ImGui::PopFont();
  ImGui::Dummy({0, logo + px(14)});

  if (ctx_.status) {
    const auto [state, text] = ctx_.status();
    const ImU32 dot = state == AppState::Buffering ? theme::accent()
                      : state == AppState::Recording ? theme::kDanger
                      : state == AppState::Paused    ? theme::kFaint
                                                     : theme::kWarning;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(2));
    ui::statusPill(f, dot, text);
  }
  ImGui::Dummy({0, px(18)});

  if (ui::navItem(f, icon::kGallery, tr("nav.gallery"), page_ == Page::Gallery)) showGallery();
  ImGui::Dummy({0, px(16)});
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(14));
  ImGui::PushFont(f.smallText);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::kFaint);
  ImGui::TextUnformatted(tr("nav.header"));
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy({0, px(4)});
  struct Item {
    SettingsSection s;
    const char* glyph;
    const char* key;
  };
  static const Item items[] = {
      {SettingsSection::Capture, icon::kMonitor, "sec.capture"}, {SettingsSection::Replay, icon::kHistory, "sec.replay"},
      {SettingsSection::Audio, icon::kVolume, "sec.audio"},       {SettingsSection::Hotkeys, icon::kKeyboard, "sec.hotkeys"},
      {SettingsSection::Output, icon::kFolder, "sec.output"},     {SettingsSection::General, icon::kSettings, "sec.general"},
  };
  for (const auto& it : items)
    if (ui::navItem(f, it.glyph, tr(it.key), page_ == Page::Settings && section_ == it.s)) showSettings(it.s);

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

}  // namespace clipper
