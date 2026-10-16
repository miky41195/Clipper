#include <doctest/doctest.h>
#include "TestUtil.h"
#include "ui/SettingsPage.h"
#include "ui/UiPreview.h"

using namespace clipper;

TEST_CASE("apply sends the draft and clears dirty state") {
  UiContext ctx = previewContext();
  Settings received;
  ctx.applySettings = [&](const Settings& s) {
    received = s;
    return std::string{};
  };
  SettingsPage page(ctx);
  page.form().draft.fps = 144;
  CHECK(page.dirty());
  CHECK(page.applyDraft());
  CHECK(received.fps == 144);
  CHECK_FALSE(page.dirty());
  CHECK(page.lastError().empty());
}

TEST_CASE("apply error is kept and the draft stays dirty") {
  UiContext ctx = previewContext();
  ctx.applySettings = [](const Settings&) { return std::string("Scorciatoia Alt+F10 già usata"); };
  SettingsPage page(ctx);
  page.form().draft.hotkeys.saveClip = "Alt+F10";
  page.form().draft.fps = 30;
  CHECK_FALSE(page.applyDraft());
  CHECK(page.lastError() == "Scorciatoia Alt+F10 già usata");
  CHECK(page.dirty());
}

TEST_SUITE("gpu") {
  TEST_CASE("every settings section renders") {
    const auto dir = freshTempDir("ui_sections");
    for (const char* page : {"settings.capture", "settings.replay", "settings.audio", "settings.hotkeys", "settings.output",
                             "settings.general"}) {
      const auto png = dir / (std::string(page) + ".png");
      CHECK(renderPreview(page, png, 1.0f));
      CHECK(std::filesystem::file_size(png) > 10'000);
    }
  }
}
