#include <doctest/doctest.h>
#include <cstring>
#include "ui/Color.h"
#include "core/I18n.h"

using namespace clipper;

TEST_CASE("parseHexColor") {
  CHECK(parseHexColor("#3DDC84") == 0xFF84DC3Du);
  CHECK(parseHexColor("3ddc84") == 0xFF84DC3Du);
  CHECK_FALSE(parseHexColor("#3DDC8").has_value());
  CHECK_FALSE(parseHexColor("#GGGGGG").has_value());
  CHECK_FALSE(parseHexColor("").has_value());
  CHECK(toHexColor(0xFF84DC3Du) == "#3DDC84");
}

TEST_CASE("every i18n key has italian and english text") {
  for (const auto& e : i18nTable()) {
    CHECK(std::strlen(e.it) > 0);
    CHECK(std::strlen(e.en) > 0);
  }
  CHECK(i18nTable().size() > 20);
}

TEST_CASE("tray, toast and warning texts are translated") {
  setLanguage("en");
  CHECK(std::string(tr("tray.quit")) == "Quit");
  CHECK(std::string(tr("toast.saved")) == "Clip saved");
  CHECK(trf("hk.err.taken", std::string("Alt+F10")) == "Shortcut Alt+F10 is already used by another program");
  setLanguage("it");
  CHECK(std::string(tr("tray.quit")) == "Esci");
  CHECK(trf("toast.rec.started.body", std::string("Alt+Shift+F9")) == "Premi Alt+Shift+F9 per fermare");
}

TEST_CASE("tr follows the language and falls back to the key") {
  setLanguage("it");
  CHECK(std::string(tr("nav.settings")) == "Impostazioni");
  setLanguage("en");
  CHECK(std::string(tr("nav.settings")) == "Settings");
  CHECK(std::string(tr("no.such.key")) == "no.such.key");
  setLanguage("xx");
  CHECK(std::string(tr("nav.settings")) == "Impostazioni");
}
