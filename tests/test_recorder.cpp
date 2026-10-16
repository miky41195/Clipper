#include <doctest/doctest.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include "Ffprobe.h"
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "session/Recorder.h"

using namespace clipper;
namespace fs = std::filesystem;

namespace {
struct Media {
  std::vector<TrackInfo> tracks;
  std::vector<Packet> packets;
};

Media makeMedia() {
  static const GpuDevice g = createGpuDevice();
  Media m;
  TrackInfo v;
  m.packets = encodeTestVideo(g, {Codec::H264, 640, 360, 30, 2000, false}, 2.0, seconds(10), nullptr, &v);
  TrackInfo a;
  a.id = 1;
  a.kind = TrackKind::Audio;
  a.name = "Mix";
  {
    AudioEncoder enc({48000, 2, 160}, 1, [&](Packet p) { m.packets.push_back(std::move(p)); });
    std::vector<int16_t> chunk(960, 100);
    for (int c = 0; c < 200; ++c) enc.encode(chunk.data(), 480, seconds(10) + Ticks(c) * 100'000);
    enc.flush();
    a.extradata = enc.extradata();
  }
  std::stable_sort(m.packets.begin(), m.packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
  m.tracks = {v, a};
  return m;
}
}  // namespace

TEST_SUITE("gpu") {
  TEST_CASE("recording to mp4 contains all streams and no leftovers") {
    const auto dir = freshTempDir("rec_mp4");
    const auto m = makeMedia();
    PacketBus bus;
    int keyRequests = 0;
    Recorder rec(bus, m.tracks, dir / "rec.mp4", "mp4", [&] { ++keyRequests; });
    for (const auto& p : m.packets) bus.publish(p);
    const auto out = rec.stop();
    CHECK(out == dir / "rec.mp4");
    CHECK(keyRequests == 1);
    const auto j = ffprobe(out);
    REQUIRE(j["streams"].size() == 2);
    CHECK(streamTag(j["streams"][1], "handler_name") == "Mix");
    CHECK(rec.duration() >= seconds(1.9));
    size_t files = 0;
    for (auto& e : fs::directory_iterator(dir)) files += 1 + 0 * e.path().empty();
    CHECK(files == 1);
  }

  TEST_CASE("recording to mkv") {
    const auto dir = freshTempDir("rec_mkv");
    const auto m = makeMedia();
    PacketBus bus;
    Recorder rec(bus, m.tracks, dir / "rec.mkv", "mkv", [] {});
    for (const auto& p : m.packets) bus.publish(p);
    CHECK(ffprobe(rec.stop())["streams"].size() == 2);
  }

  TEST_CASE("detached recorder finishes after its bus is gone") {
    const auto dir = freshTempDir("rec_detach");
    const auto m = makeMedia();
    auto bus = std::make_unique<PacketBus>();
    Recorder rec(*bus, m.tracks, dir / "rec.mp4", "mp4", [] {});
    for (const auto& p : m.packets) bus->publish(p);
    rec.detach();
    bus.reset();
    CHECK(fs::exists(rec.stop()));
  }

  TEST_CASE("a slow disk does not block the publisher") {
    const auto dir = freshTempDir("rec_slow");
    const auto m = makeMedia();
    PacketBus bus;
    Recorder rec(bus, m.tracks, dir / "rec.mp4", "mp4", [] {});
    rec.setWriteHookForTest([] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& p : m.packets) bus.publish(p);  // ~260 packets: 1.3 s if written inline
    const auto publishMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("publish took " << publishMs << " ms for " << m.packets.size() << " packets");
    CHECK(publishMs < 200);
    const auto out = rec.stop();  // drains the queue
    const auto j = ffprobe(out);
    REQUIRE(j["streams"].size() == 2);
    CHECK(probeDuration(j) == doctest::Approx(2.0).epsilon(0.1));
  }

  TEST_CASE("destructor finalizes the recording") {
    const auto dir = freshTempDir("rec_dtor");
    const auto m = makeMedia();
    PacketBus bus;
    {
      Recorder rec(bus, m.tracks, dir / "rec.mp4", "mp4", [] {});
      for (const auto& p : m.packets) bus.publish(p);
    }
    CHECK(fs::exists(dir / "rec.mp4"));
    bus.publish(m.packets.front());  // recorder unsubscribed: must not crash
  }
}
