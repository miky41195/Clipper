#include <doctest/doctest.h>
#include <fstream>
#include "TestUtil.h"
#include "app/Startup.h"

using namespace clipper;
namespace fs = std::filesystem;

TEST_CASE("startup cleanup empties the buffer and handles leftovers") {
  const auto buf = freshTempDir("startup_buf");
  const auto out = freshTempDir("startup_out");
  std::ofstream(buf / "seg_00000001.bin") << "x";
  std::ofstream(out / "Gioco_2026.mp4.tmp") << "x";
  std::ofstream(out / "Gioco_2026.rec.mkv.tmp") << "x";
  std::ofstream(out / "keep.mp4") << "x";
  cleanupOnStartup(buf, out);
  CHECK(fs::is_empty(buf));
  CHECK_FALSE(fs::exists(out / "Gioco_2026.mp4.tmp"));
  CHECK_FALSE(fs::exists(out / "Gioco_2026.rec.mkv.tmp"));
  CHECK(fs::exists(out / "Gioco_2026_recuperata.mkv"));
  CHECK(fs::exists(out / "keep.mp4"));
}

TEST_CASE("startup cleanup tolerates missing directories") {
  cleanupOnStartup("Z:/does/not/exist", "Z:/nope");
}
