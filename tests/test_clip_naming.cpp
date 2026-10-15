#include <doctest/doctest.h>
#include <ctime>
#include <fstream>
#include "TestUtil.h"
#include "app/ClipNaming.h"

using namespace clipper;
namespace fs = std::filesystem;

static std::chrono::system_clock::time_point localTime(int y, int mo, int d, int h, int mi, int s) {
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = s;
  tm.tm_isdst = -1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

TEST_CASE("template expansion") {
  const auto t = localTime(2026, 10, 16, 21, 5, 9);
  CHECK(expandNameTemplate("{app}_{date}_{time}", "Valorant", t) == "Valorant_2026-10-16_21-05-09");
  CHECK(expandNameTemplate("clip {date}", "x", t) == "clip 2026-10-16");
  CHECK(expandNameTemplate("{unknown}", "x", t) == "{unknown}");
}

TEST_CASE("file names are sanitized") {
  CHECK(sanitizeFileName("a<b>c:d") == "a_b_c_d");
  CHECK(sanitizeFileName("x/y\\z|w?v*u\"") == "x_y_z_w_v_u_");
  CHECK(sanitizeFileName("  name.. ") == "name");
  CHECK(sanitizeFileName("") == "clip");
  CHECK(sanitizeFileName("...") == "clip");
  CHECK(sanitizeFileName("Ränk 日本") == "Ränk 日本");
}

TEST_CASE("uniquePath appends a counter") {
  const auto dir = freshTempDir("naming_unique");
  CHECK(uniquePath(dir, "clip", ".mp4") == dir / L"clip.mp4");
  std::ofstream(dir / L"clip.mp4") << "x";
  CHECK(uniquePath(dir, "clip", ".mp4") == dir / L"clip (2).mp4");
  std::ofstream(dir / L"clip (2).mp4") << "x";
  CHECK(uniquePath(dir, "clip", ".mp4") == dir / L"clip (3).mp4");
  CHECK(uniquePath(dir, "è", ".mkv") == dir / L"è.mkv");
}
