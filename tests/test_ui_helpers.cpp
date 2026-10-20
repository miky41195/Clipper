#include <windows.h>
#include <doctest/doctest.h>
#include "platform/Autostart.h"
#include "ui/AudioApps.h"
#include "ui/HotkeyCapture.h"

using namespace clipper;

TEST_CASE("hotkey from pressed keys") {
  CHECK(hotkeyFromKeys(true, false, true, false, 'S') == "Ctrl+Shift+S");
  CHECK(hotkeyFromKeys(false, true, false, false, 0x79) == "Alt+F10");
  CHECK(hotkeyFromKeys(false, false, false, false, 0x78) == "F9");
  CHECK_FALSE(hotkeyFromKeys(false, false, false, false, 'S').has_value());
  CHECK_FALSE(hotkeyFromKeys(true, false, false, false, 0x11).has_value());  // only Ctrl itself
  CHECK_FALSE(hotkeyFromKeys(true, false, false, false, 0xBA).has_value());  // unsupported key
}

TEST_CASE("audio app rows merge live apps and blacklist") {
  const std::vector<AppGroup> live = {{"spotify.exe", {1}}, {"game.exe", {2}}};
  auto nameOf = [](const std::string& exe) { return exe == "game.exe" ? std::string("Il Gioco") : std::string("Spotify"); };
  const auto rows = audioAppRows(live, {"discord.exe", "spotify.exe"}, nameOf);
  REQUIRE(rows.size() == 3);
  CHECK(rows[0].exe == "discord.exe");  // sorted by display name: Discord, Il Gioco, Spotify
  CHECK(rows[0].blacklisted);
  CHECK(rows[0].name == "Discord");     // not live: falls back to exe stem
  CHECK(rows[1].name == "Il Gioco");
  CHECK_FALSE(rows[1].blacklisted);
  CHECK(rows[2].blacklisted);
  CHECK(audioAppRows({}, {}, nameOf).empty());
}

TEST_CASE("toggleBlacklist adds and removes") {
  std::vector<std::string> bl = {"a.exe"};
  toggleBlacklist(bl, "b.exe", true);
  toggleBlacklist(bl, "a.exe", false);
  toggleBlacklist(bl, "b.exe", true);
  CHECK(bl == std::vector<std::string>{"b.exe"});
}

TEST_CASE("autostart registry value round trip") {
  const wchar_t* name = L"ClipperTestAutostart";
  setAutostart(true, L"C:\\x\\clipper.exe", name);
  CHECK(autostartEnabled(name));
  setAutostart(false, L"", name);
  CHECK_FALSE(autostartEnabled(name));
}

TEST_CASE("autostart works when the Run key does not exist yet") {
  // Point HKCU at an empty key for this process, like a fresh profile without ...\Run.
  const wchar_t* hive = L"Software\\ClipperTestEmptyHive";
  HKEY empty = nullptr;
  REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, hive, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &empty, nullptr) == ERROR_SUCCESS);
  REQUIRE(RegOverridePredefKey(HKEY_CURRENT_USER, empty) == ERROR_SUCCESS);
  setAutostart(true, L"C:\\x\\clipper.exe", L"ClipperTestAutostart");
  const bool on = autostartEnabled(L"ClipperTestAutostart");
  RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
  RegCloseKey(empty);
  RegDeleteTreeW(HKEY_CURRENT_USER, hive);
  RegDeleteKeyW(HKEY_CURRENT_USER, hive);
  CHECK(on);
}
