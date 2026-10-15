#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include "BufferUtil.h"
#include "Ffprobe.h"
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "output/ClipWriter.h"

using namespace clipper;
namespace fs = std::filesystem;

namespace {

struct TestMedia {
  TrackInfo video;
  std::vector<TrackInfo> audio;
  std::vector<Packet> packets;  // sorted by pts
};

std::vector<Packet> encodeAudio(int track, double secs, Ticks start, std::vector<uint8_t>* extradata) {
  std::vector<Packet> out;
  AudioEncoder enc({48000, 2, 160}, track, [&](Packet p) { out.push_back(std::move(p)); });
  std::vector<int16_t> chunk(480 * 2);
  const int chunks = int(secs * 100);
  for (int c = 0; c < chunks; ++c) {
    for (int i = 0; i < 480; ++i)
      chunk[i * 2] = chunk[i * 2 + 1] = int16_t(8000 * std::sin(2 * std::numbers::pi * 440 * (c * 480 + i) / 48000.0));
    enc.encode(chunk.data(), 480, start + Ticks(c) * 100'000);
  }
  enc.flush();
  if (extradata) *extradata = enc.extradata();
  return out;
}

TestMedia makeMedia(double secs, std::vector<std::pair<const char*, double>> audioTracks /* name, start offset s */) {
  static const GpuDevice g = createGpuDevice();
  TestMedia m;
  const Ticks t0 = seconds(100);
  m.packets = encodeTestVideo(g, {Codec::H264, 640, 360, 30, 2000, false}, secs, t0, nullptr, &m.video);
  int id = 1;
  for (auto [name, offset] : audioTracks) {
    TrackInfo t;
    t.id = id;
    t.kind = TrackKind::Audio;
    t.codec = Codec::AAC;
    t.name = name;
    auto pk = encodeAudio(id, secs - offset, t0 + seconds(offset), &t.extradata);
    m.audio.push_back(t);
    m.packets.insert(m.packets.end(), pk.begin(), pk.end());
    ++id;
  }
  std::stable_sort(m.packets.begin(), m.packets.end(), [](const Packet& a, const Packet& b) { return a.pts < b.pts; });
  return m;
}

std::vector<TrackInfo> allTracks(const TestMedia& m) {
  std::vector<TrackInfo> t{m.video};
  t.insert(t.end(), m.audio.begin(), m.audio.end());
  return t;
}

void writeAll(const TestMedia& m, const fs::path& file, const char* container) {
  ClipWriter w(file, container, allTracks(m));
  for (const auto& p : m.packets) w.write(p);
  w.finish();
}

}  // namespace

TEST_SUITE("gpu") {
  TEST_CASE("mp4 with video and two named audio tracks") {
    const auto dir = freshTempDir("cw_mp4");
    const auto m = makeMedia(2.0, {{"Microfono", 0}, {"Discord", 0}});
    writeAll(m, dir / "clip.mp4", "mp4");
    CHECK_FALSE(fs::exists(dir / "clip.mp4.tmp"));
    const auto j = ffprobe(dir / "clip.mp4");
    REQUIRE(j["streams"].size() == 3);
    CHECK(j["streams"][0]["codec_name"] == "h264");
    CHECK(j["streams"][1]["codec_name"] == "aac");
    CHECK(streamTag(j["streams"][1], "handler_name") == "Microfono");
    CHECK(streamTag(j["streams"][2], "handler_name") == "Discord");
    CHECK(probeDuration(j) == doctest::Approx(2.0).epsilon(0.08));
  }

  TEST_CASE("mkv keeps titles") {
    const auto dir = freshTempDir("cw_mkv");
    const auto m = makeMedia(2.0, {{"Mix", 0}});
    writeAll(m, dir / "clip.mkv", "mkv");
    const auto j = ffprobe(dir / "clip.mkv");
    REQUIRE(j["streams"].size() == 2);
    CHECK(streamTag(j["streams"][1], "title") == "Mix");
  }

  TEST_CASE("starts at the first keyframe") {
    const auto dir = freshTempDir("cw_key");
    auto m = makeMedia(2.0, {{"Mix", 0}});
    int dropped = 0;
    std::erase_if(m.packets, [&](const Packet& p) { return p.track == kVideoTrack && dropped++ < 10; });
    writeAll(m, dir / "clip.mp4", "mp4");
    CHECK(probeDuration(ffprobe(dir / "clip.mp4")) == doctest::Approx(1.0).epsilon(0.15));
  }

  TEST_CASE("unfinished writer leaves no files") {
    const auto dir = freshTempDir("cw_abort");
    const auto m = makeMedia(1.0, {{"Mix", 0}});
    {
      ClipWriter w(dir / "clip.mp4", "mp4", allTracks(m));
      for (const auto& p : m.packets) w.write(p);
    }
    CHECK(fs::is_empty(dir));
  }

  TEST_CASE("writeSnapshot from a ReplayBuffer") {
    const auto dir = freshTempDir("cw_snap");
    const auto m = makeMedia(3.0, {{"Mix", 0}});
    BufferConfig c;
    c.duration = seconds(10);
    ReplayBuffer b(c);
    for (const auto& t : allTracks(m)) b.setTrack(t);
    for (const auto& p : m.packets) b.push(p);
    writeSnapshot(b.snapshot(seconds(1.5)), dir / "snap.mp4", "mp4");
    const double d = probeDuration(ffprobe(dir / "snap.mp4"));
    CHECK(d >= 1.4);
    CHECK(d <= 2.6);
  }

  TEST_CASE("remux mkv to mp4 keeps streams and names") {
    const auto dir = freshTempDir("cw_remux");
    const auto m = makeMedia(1.5, {{"Microfono", 0}, {"Gioco", 0}});
    writeAll(m, dir / "rec.mkv", "mkv");
    remux(dir / "rec.mkv", dir / "rec.mp4");
    const auto j = ffprobe(dir / "rec.mp4");
    REQUIRE(j["streams"].size() == 3);
    CHECK(streamTag(j["streams"][2], "handler_name") == "Gioco");
  }

  TEST_CASE("unicode output directory") {
    const auto dir = freshTempDir("cw_àèì 日本");
    const auto m = makeMedia(1.0, {{"Ränk", 0}});
    writeAll(m, dir / L"clip è.mp4", "mp4");
    CHECK(fs::exists(dir / L"clip è.mp4"));
    CHECK(ffprobe(dir / L"clip è.mp4")["streams"].size() == 2);
  }

  TEST_CASE("audio track that starts late still muxes") {
    const auto dir = freshTempDir("cw_late");
    const auto m = makeMedia(2.0, {{"Mix", 0}, {"Spotify", 1.0}});
    writeAll(m, dir / "clip.mp4", "mp4");
    const auto j = ffprobe(dir / "clip.mp4");
    REQUIRE(j["streams"].size() == 3);
    CHECK(probeDuration(j) == doctest::Approx(2.0).epsilon(0.08));
  }
}
