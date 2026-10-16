#include <doctest/doctest.h>
#include <fstream>
#include "TestUtil.h"
#include "gallery/FileOps.h"

using namespace clipper;
namespace fs = std::filesystem;

TEST_CASE("renameClip sanitizes, keeps the extension and avoids collisions") {
  const auto dir = freshTempDir("ops_rename");
  std::ofstream(dir / "a.mp4") << "x";
  std::ofstream(dir / "taken.mp4") << "y";
  auto p = renameClip(dir / "a.mp4", "taken");
  CHECK(p == dir / L"taken (2).mp4");
  CHECK(fs::exists(p));
  p = renameClip(p, "bad:name?");
  CHECK(p == dir / L"bad_name_.mp4");
  CHECK(renameClip(p, "bad_name_") == p);  // same name: no-op
  p = renameClip(p, "  Ränk è ");
  CHECK(p == dir / L"Ränk è.mp4");
  CHECK(fs::exists(p));
}

TEST_CASE("recycle reports failure for a missing file") {
  const auto dir = freshTempDir("ops_recycle");
  CHECK_FALSE(recycle(dir / "missing.mp4"));
}
