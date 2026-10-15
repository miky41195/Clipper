#include <doctest/doctest.h>
#include <windows.h>
#include "platform/Hotkeys.h"

using namespace clipper;

static HWND messageWindow() {
  return CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

TEST_SUITE("desktop") {
  TEST_CASE("invalid hotkey strings are reported") {
    HWND w = messageWindow();
    {
      HotkeyManager hk(w);
      HotkeySettings s;
      s.saveClip = "Banana";
      s.toggleRecord = "Ctrl+Alt+Shift+F20";
      s.openGallery = "Ctrl+Alt+Shift+F21";
      const auto errors = hk.apply(s);
      REQUIRE(errors.size() == 1);
      CHECK(errors.contains(Action::SaveClip));
    }
    DestroyWindow(w);
  }

  TEST_CASE("a combination already taken is reported as conflict") {
    HWND a = messageWindow();
    HWND b = messageWindow();
    {
      HotkeySettings s;
      s.saveClip = "Ctrl+Alt+Shift+F22";
      s.toggleRecord = "Ctrl+Alt+Shift+F23";
      s.openGallery = "Ctrl+Alt+Shift+F24";
      HotkeyManager first(a);
      CHECK(first.apply(s).empty());
      HotkeyManager second(b);
      const auto errors = second.apply(s);
      CHECK(errors.size() == 3);
      CHECK(HotkeyManager::fromId(1) == Action::SaveClip);
      CHECK_FALSE(HotkeyManager::fromId(99).has_value());
    }
    DestroyWindow(a);
    DestroyWindow(b);
  }
}
