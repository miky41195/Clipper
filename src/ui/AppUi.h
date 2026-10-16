#pragma once
#include "ui/Fonts.h"
#include "ui/SettingsPage.h"
#include "ui/UiContext.h"
#include <functional>
#include <memory>

namespace clipper {

// Pages shown in the app window plus the sidebar that switches between them.
class AppUi {
public:
  enum class Page { Gallery, Settings };

  explicit AppUi(UiContext& ctx);
  ~AppUi();

  void draw(const UiFonts& f);
  void showGallery();
  void showSettings(SettingsSection s = SettingsSection::Capture);
  void onOpen();  // window just opened: refresh data
  Page page() const { return page_; }
  SettingsPage& settings() { return settings_; }
  // Gallery page drawer, installed by the gallery module (plan 3); empty state when absent.
  std::function<void(const UiFonts&)> galleryDrawer;
  std::function<void()> galleryOnOpen;

private:
  void drawSidebar(const UiFonts& f);
  void drawGalleryPlaceholder(const UiFonts& f);

  UiContext& ctx_;
  SettingsPage settings_;
  Page page_ = Page::Gallery;
  SettingsSection section_ = SettingsSection::Capture;
};

}  // namespace clipper
