#include <doctest/doctest.h>
#include <cmath>
#include <mutex>
#include <numbers>
#include "Decode.h"
#include "MediaGen.h"
#include "TestUtil.h"
#include "audio/AudioHub.h"
#include "encode/AudioEncoderBank.h"
#include "output/ClipWriter.h"

using namespace clipper;

TEST_SUITE("gpu") {
  TEST_CASE("flash and beep at the same instant stay within 20 ms") {
    const auto g = createGpuDevice();
    const Ticks t0 = seconds(500);
    const int fps = 60;
    BufferConfig bc;
    bc.duration = seconds(30);
    ReplayBuffer buffer(bc);
    std::mutex mu;
    auto sink = [&](Packet p) {
      std::lock_guard lk(mu);
      buffer.push(std::move(p));
    };

    // Video: black, white for 6 frames starting exactly at t0 + 1 s.
    TrackInfo video;
    const auto vpk = encodeTestVideo(
        g, {Codec::H264, 640, 360, fps, 3000, false}, 3.0, t0,
        [&](int i) { return uint8_t(i >= fps && i < fps + 6 ? 235 : 16); }, &video);
    buffer.setTrack(video);
    for (const auto& p : vpk) sink(p);

    // Audio: silence, beep at t0 + 1 s for 60 ms, pushed with capture-style timestamps.
    AudioEncoderBank bank(160, sink);
    AudioHub hub(
        AudioHubConfig{}, [&](int t, const int16_t* p, size_t f, Ticks pts) { bank.onChunk(t, p, f, pts); },
        [&](const TrackInfo& t) { buffer.setTrack(bank.onTrack(t)); }, [&](int id) { bank.onTrackEnd(id); });
    const int mic = hub.addTrack("Microfono");
    auto src = std::make_shared<PcmSource>(48000, 2, 48000 * 4);
    hub.attach(mic, src);
    hub.start(t0);
    std::vector<int16_t> chunk(480 * 2);
    for (int c = 0; c < 300; ++c) {
      for (int i = 0; i < 480; ++i) {
        const int n = c * 480 + i;
        const bool beep = n >= 48000 && n < 48000 + 2880;
        chunk[i * 2] = chunk[i * 2 + 1] = beep ? int16_t(20000 * std::sin(2 * std::numbers::pi * 1000 * n / 48000.0)) : 0;
      }
      src->push(chunk.data(), 480, t0 + Ticks(c) * 100'000);
      hub.tick(t0 + Ticks(c + 1) * 100'000);
    }
    bank.onTrackEnd(mic);
    bank.onTrackEnd(hub.mixTrackId());

    const auto dir = freshTempDir("sync");
    writeSnapshot(buffer.snapshot(seconds(30)), dir / "sync.mp4", "mp4");
    const double flash = firstEventTime(dir / "sync.mp4", AVMEDIA_TYPE_VIDEO);
    const double beep = firstEventTime(dir / "sync.mp4", AVMEDIA_TYPE_AUDIO);
    MESSAGE("flash " << flash << " s, beep " << beep << " s, diff " << (beep - flash) * 1000 << " ms");
    REQUIRE(flash >= 0);
    REQUIRE(beep >= 0);
    CHECK(std::fabs(beep - flash) < 0.020);
  }
}
