#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include "Decode.h"
#include "Ffprobe.h"
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "gallery/Exporter.h"
#include "output/ClipWriter.h"

using namespace clipper;
namespace fs = std::filesystem;

namespace {
// 2 s clip: track "Gioco" = 1 kHz tone at -6 dB, track "Spotify" = silence.
fs::path makeSource(const fs::path& dir) {
  static const GpuDevice g = createGpuDevice();
  TrackInfo v;
  auto packets = encodeTestVideo(g, {Codec::H264, 320, 180, 30, 800, false}, 2.0, 0, nullptr, &v);
  std::vector<TrackInfo> tracks{v};
  for (int id : {1, 2}) {
    TrackInfo a;
    a.id = id;
    a.kind = TrackKind::Audio;
    a.name = id == 1 ? "Gioco" : "Spotify";
    AudioEncoder enc({48000, 2, 128}, id, [&](Packet p) { packets.push_back(std::move(p)); });
    std::vector<int16_t> pcm(960);
    for (int c = 0; c < 200; ++c) {
      for (int i = 0; i < 480; ++i) {
        const auto s = id == 1 ? int16_t(16000 * std::sin(2 * std::numbers::pi * 1000 * (c * 480 + i) / 48000.0)) : int16_t(0);
        pcm[size_t(i) * 2] = pcm[size_t(i) * 2 + 1] = s;
      }
      enc.encode(pcm.data(), 480, Ticks(c) * 100'000);
    }
    enc.flush();
    a.extradata = enc.extradata();
    tracks.push_back(a);
  }
  std::stable_sort(packets.begin(), packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
  const auto file = dir / "src.mp4";
  ClipWriter w(file, "mp4", tracks);
  w.setApp("TestGame");
  for (const auto& p : packets) w.write(p);
  w.finish();
  return file;
}
}  // namespace

TEST_SUITE("gpu") {
  TEST_CASE("export keeps only the chosen tracks, mixed") {
    const auto dir = freshTempDir("export");
    const auto src = makeSource(dir);

    const auto onlySpotify = exportClip(src, {{false, true}, {1.f, 1.f}, false}, {});
    auto j = ffprobe(onlySpotify);
    REQUIRE(j["streams"].size() == 2);
    CHECK(streamTag(j["streams"][1], "handler_name") == "Mix");
    CHECK(audioPeak(onlySpotify) < 0.01);
    CHECK(probeDuration(j) == doctest::Approx(2.0).epsilon(0.08));
    CHECK(onlySpotify.filename() == L"src_export.mp4");

    const auto onlyGame = exportClip(src, {{true, false}, {1.f, 1.f}, false}, {});
    CHECK(audioPeak(onlyGame) > 0.3);
    CHECK(onlyGame.filename() == L"src_export (2).mp4");

    const auto quieter = exportClip(src, {{true, false}, {0.25f, 1.f}, false}, {});
    CHECK(audioPeak(quieter) < 0.25);
    CHECK(audioPeak(quieter) > 0.05);
  }

  TEST_CASE("export with no tracks is video only; alsoSeparate adds the kept tracks") {
    const auto dir = freshTempDir("export2");
    const auto src = makeSource(dir);
    float last = 0;
    const auto videoOnly = exportClip(src, {{false, false}, {1.f, 1.f}, false}, [&](float p) { last = p; });
    CHECK(ffprobe(videoOnly)["streams"].size() == 1);
    CHECK(last == doctest::Approx(1.f));
    const auto separate = exportClip(src, {{true, true}, {1.f, 1.f}, true}, {});
    const auto j = ffprobe(separate);
    REQUIRE(j["streams"].size() == 4);
    CHECK(streamTag(j["streams"][2], "handler_name") == "Gioco");
    CHECK(streamTag(j["streams"][3], "handler_name") == "Spotify");
    CHECK(j["format"]["tags"].value("comment", "") == "clipper:app=TestGame");
  }
}
