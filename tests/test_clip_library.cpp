#include <doctest/doctest.h>
#include <ctime>
#include <fstream>
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "gallery/ClipLibrary.h"
#include "output/ClipWriter.h"
#include "core/I18n.h"

using namespace clipper;
namespace fs = std::filesystem;

namespace {
// Writes a ~1 s clip with one audio track named `track`, tagged with `app` (empty = untagged).
void makeClip(const fs::path& file, const std::string& app, const char* track = "Mix") {
  static const GpuDevice g = createGpuDevice();
  TrackInfo v;
  auto packets = encodeTestVideo(g, {Codec::H264, 320, 180, 30, 800, false}, 1.0, 0, nullptr, &v);
  TrackInfo a;
  a.id = 1;
  a.kind = TrackKind::Audio;
  a.name = track;
  {
    AudioEncoder enc({48000, 2, 96}, 1, [&](Packet p) { packets.push_back(std::move(p)); });
    std::vector<int16_t> pcm(960, 0);
    for (int c = 0; c < 100; ++c) enc.encode(pcm.data(), 480, Ticks(c) * 100'000);
    enc.flush();
    a.extradata = enc.extradata();
  }
  std::stable_sort(packets.begin(), packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
  ClipWriter w(file, file.extension() == ".mkv" ? "mkv" : "mp4", {v, a});
  if (!app.empty()) w.setApp(app);
  for (const auto& p : packets) w.write(p);
  w.finish();
}

ClipInfo info(const char* name, const char* app, std::chrono::system_clock::time_point t) {
  ClipInfo c;
  c.name = name;
  c.app = app;
  c.modified = t;
  return c;
}

std::chrono::system_clock::time_point localDay(int y, int m, int d, int h = 12) {
  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_isdst = -1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}
}  // namespace

TEST_CASE("groupByDay labels today, yesterday and older dates") {
  setLanguage("it");
  const auto now = localDay(2026, 10, 16, 21);
  const std::vector<ClipInfo> clips{info("a", "X", localDay(2026, 10, 16, 9)), info("b", "X", localDay(2026, 10, 15)),
                                    info("c", "X", localDay(2026, 10, 14)), info("d", "X", localDay(2026, 10, 14, 8))};
  const auto groups = groupByDay(clips, now);
  REQUIRE(groups.size() == 3);
  CHECK(groups[0].first == "Oggi");
  CHECK(groups[1].first == "Ieri");
  CHECK(groups[2].first == "14 ottobre 2026");
  CHECK(groups[2].second.size() == 2);
}

TEST_CASE("search matches name or app, case-insensitive") {
  const auto c = info("Clutch_ace", "Valorant", std::chrono::system_clock::now());
  CHECK(matches(c, ""));
  CHECK(matches(c, "ACE"));
  CHECK(matches(c, "valo"));
  CHECK_FALSE(matches(c, "minecraft"));
}

TEST_SUITE("gpu") {
  TEST_CASE("probe reads duration, tracks and app tag") {
    const auto dir = freshTempDir("lib_probe");
    makeClip(dir / "a.mp4", "The Bazaar", "Microfono");
    const auto c = probeClip(dir / "a.mp4");
    REQUIRE(c.has_value());
    CHECK(c->app == "The Bazaar");
    CHECK(c->name == "a");
    CHECK(c->seconds == doctest::Approx(1.0).epsilon(0.15));
    REQUIRE(c->tracks.size() == 1);
    CHECK(c->tracks[0] == "Microfono");
    CHECK(c->bytes > 0);
  }

  TEST_CASE("library skips temp files, labels foreign clips and drops deleted ones") {
    const auto dir = freshTempDir("lib_scan");
    makeClip(dir / "mine.mp4", "Valorant");
    makeClip(dir / "foreign.mkv", "");
    std::ofstream(dir / "half.mp4.tmp") << "x";
    std::ofstream(dir / "notes.txt") << "x";
    ClipLibrary lib(dir, dir / "cache.json");
    auto clips = lib.refresh();
    REQUIRE(clips.size() == 2);
    const auto* foreign = clips[0].name == "foreign" ? &clips[0] : &clips[1];
    CHECK(foreign->app == "—");
    CHECK(lib.probeCount() == 2);
    clips = lib.refresh();
    CHECK(lib.probeCount() == 2);  // cache hit, nothing re-probed
    fs::remove(dir / "mine.mp4");
    clips = lib.refresh();
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].name == "foreign");
    ClipLibrary reopened(dir, dir / "cache.json");
    reopened.refresh();
    CHECK(reopened.probeCount() == 0);  // persisted cache
  }
}
