#include <doctest/doctest.h>
#include "core/Hotkey.h"

using namespace clipper;

TEST_CASE("parse common combinations") {
  CHECK(parseHotkey("Alt+F10") == Hotkey{kModAlt, 0x79});
  CHECK(parseHotkey("ctrl + shift + s") == Hotkey{kModCtrl | kModShift, 'S'});
  CHECK(parseHotkey("Win+Alt+1") == Hotkey{kModWin | kModAlt, '1'});
  CHECK(parseHotkey("Alt+Numpad5") == Hotkey{kModAlt, 0x65});
  CHECK(parseHotkey("Control+PageDown") == Hotkey{kModCtrl, 0x22});
  CHECK(parseHotkey("F9") == Hotkey{0, 0x78});
}

TEST_CASE("reject invalid combinations") {
  CHECK_FALSE(parseHotkey("S").has_value());
  CHECK_FALSE(parseHotkey("Alt+").has_value());
  CHECK_FALSE(parseHotkey("Alt+F10+F11").has_value());
  CHECK_FALSE(parseHotkey("Alt+F25").has_value());
  CHECK_FALSE(parseHotkey("Alt+Banana").has_value());
  CHECK_FALSE(parseHotkey("").has_value());
  CHECK_FALSE(parseHotkey("Ctrl+Alt").has_value());
}

TEST_CASE("format is canonical Ctrl+Alt+Shift+Win+Key") {
  CHECK(formatHotkey({kModShift | kModCtrl, 'S'}) == "Ctrl+Shift+S");
  CHECK(formatHotkey({kModAlt, 0x79}) == "Alt+F10");
  CHECK(formatHotkey({kModAlt | kModWin, 0x2C}) == "Alt+Win+PrintScreen");
}

TEST_CASE("round trip of every named key") {
  for (const char* k : {"Space", "Enter", "Tab", "Insert", "Delete", "Home", "End", "PageUp", "PageDown", "Left", "Up",
                        "Right", "Down", "PrintScreen", "Pause", "Numpad0", "F1", "F24", "A", "Z", "0", "9"}) {
    const std::string text = std::string("Ctrl+") + k;
    const auto hk = parseHotkey(text);
    REQUIRE(hk.has_value());
    CHECK(formatHotkey(*hk) == text);
  }
}
