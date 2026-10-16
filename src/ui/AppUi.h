#pragma once
#include "ui/Fonts.h"
#include "ui/GalleryPage.h"
#include "ui/SettingsPage.h"
#include "ui/UiContext.h"

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
  void onOpen();   // window just opened: refresh data
  void onClose();  // window closing: release GPU resources
  void clipsChanged();
  bool wantsAnimation() const;
  Page page() const { return page_; }
  SettingsPage& settings() { return settings_; }
  GalleryPage& gallery() { return gallery_; }

private:
  void drawSidebar(const UiFonts& f);

  UiContext& ctx_;
  SettingsPage settings_;
  GalleryPage gallery_;
  Page page_ = Page::Gallery;
  SettingsSection section_ = SettingsSection::Capture;
};

}  // namespace clipper
