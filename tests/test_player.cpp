#include <doctest/doctest.h>
#include <thread>
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "gallery/Player.h"
#include "output/ClipWriter.h"

using namespace clipper;
namespace fs = std::filesystem;

TEST_CASE("clampSeek keeps the target inside the clip") {
  CHECK(clampSeek(-1, 10) == 0);
  CHECK(clampSeek(5, 10) == 5);
  CHECK(clampSeek(12, 10) == doctest::Approx(9.9));
  CHECK(clampSeek(3, 0) == 0);
}

TEST_CASE("mixTracks applies gains and clamps") {
  const float a[] = {0.5f, -0.5f, 0.9f};
  const float b[] = {0.25f, 0.25f, 0.9f};
  float out[3];
  mixTracks({a, b}, {1.f, 2.f}, out, 3);
  CHECK(out[0] == doctest::Approx(1.0f));
  CHECK(out[1] == doctest::Approx(0.0f));
  CHECK(out[2] == doctest::Approx(1.0f));  // clamped
  mixTracks({a, b}, {0.f, 0.f}, out, 3);
  CHECK(out[0] == 0.f);
  mixTracks({}, {}, out, 3);
  CHECK(out[2] == 0.f);
}

namespace {
// 3 s clip: dark first second, bright afterwards; two silent audio tracks.
fs::path makeSilentClip(const fs::path& dir) {
  static const GpuDevice g = createGpuDevice();
  TrackInfo v;
  auto packets = encodeTestVideo(g, {Codec::H264, 640, 360, 30, 2000, false}, 3.0, 0,
                                 [](int i) { return uint8_t(i < 30 ? 30 : 220); }, &v);
  std::vector<TrackInfo> tracks{v};
  for (int id : {1, 2}) {
    TrackInfo a;
    a.id = id;
    a.kind = TrackKind::Audio;
    a.name = id == 1 ? "Gioco" : "Microfono";
    AudioEncoder enc({48000, 2, 96}, id, [&](Packet p) { packets.push_back(std::move(p)); });
    std::vector<int16_t> pcm(960, 0);
    for (int c = 0; c < 300; ++c) enc.encode(pcm.data(), 480, Ticks(c) * 100'000);
    enc.flush();
    a.extradata = enc.extradata();
    tracks.push_back(a);
  }
  std::stable_sort(packets.begin(), packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
  const auto file = dir / "clip.mp4";
  ClipWriter w(file, "mp4", tracks);
  for (const auto& p : packets) w.write(p);
  w.finish();
  return file;
}

bool waitFrame(Player& p, std::vector<uint8_t>& px, int& w, int& h) {
  for (int i = 0; i < 100; ++i) {
    if (p.takeFrame(px, w, h)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}
}  // namespace

TEST_SUITE("gpu") {
  TEST_CASE("player opens a clip, seeks and shows the right frame") {
    const auto dir = freshTempDir("player");
    Player p(makeSilentClip(dir));
    REQUIRE(p.ok());
    CHECK(p.duration() == doctest::Approx(3.0).epsilon(0.08));
    CHECK(p.trackNames() == std::vector<std::string>{"Gioco", "Microfono"});
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    REQUIRE(waitFrame(p, px, w, h));  // first frame while paused
    CHECK(w == 640);
    CHECK(int(px[(size_t(h / 2) * w + w / 2) * 4 + 1]) < 80);
    p.seek(1.5);
    REQUIRE(waitFrame(p, px, w, h));
    CHECK(int(px[(size_t(h / 2) * w + w / 2) * 4 + 1]) > 150);
    CHECK(p.position() == doctest::Approx(1.5).epsilon(0.05));
  }

  TEST_CASE("player advances while playing (silent tracks)") {
    const auto dir = freshTempDir("player_play");
    Player p(makeSilentClip(dir));
    REQUIRE(p.ok());
    p.setTrackEnabled(0, false);
    p.setTrackGain(1, 0.f);
    p.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const double pos = p.position();
    MESSAGE("position after 0.8 s: " << pos);
    CHECK(pos > 0.4);
    CHECK(pos < 1.3);
    p.pause();
    const double paused = p.position();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(p.position() == doctest::Approx(paused).epsilon(0.02));
  }

  TEST_CASE("missing file reports an error") {
    Player p("Z:/no/such/clip.mp4");
    CHECK_FALSE(p.ok());
    CHECK_FALSE(p.error().empty());
  }
}
