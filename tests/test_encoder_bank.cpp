#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include "Decode.h"
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoderBank.h"
#include "output/ClipWriter.h"

using namespace clipper;
namespace fs = std::filesystem;

TEST_SUITE("gpu") {
  TEST_CASE("a track that comes back is continuous: the gap becomes silence") {
    static const GpuDevice g = createGpuDevice();
    TrackInfo v;
    std::vector<Packet> packets = encodeTestVideo(g, {Codec::H264, 320, 180, 30, 800, false}, 3.0, 0, nullptr, &v);
    std::vector<Packet> audio;
    AudioEncoderBank bank(128, [&](Packet p) { audio.push_back(std::move(p)); });
    TrackInfo t;
    t.id = 4;
    t.kind = TrackKind::Audio;
    t.name = "Spotify";
    TrackInfo info = bank.onTrack(t);
    std::vector<int16_t> pcm(960);
    for (int c = 0; c < 300; ++c) {
      if (c == 100) bank.onTrackEnd(4);  // app silent: track retired at 1 s
      if (c == 200) bank.onTrack(t);     // and back at 2 s
      if (c >= 100 && c < 200) continue;
      for (int i = 0; i < 480; ++i)
        pcm[size_t(i) * 2] = pcm[size_t(i) * 2 + 1] =
            c >= 200 ? int16_t(16000 * std::sin(2 * std::numbers::pi * 440 * (c * 480 + i) / 48000.0)) : int16_t(0);
      bank.onChunk(4, pcm.data(), 480, Ticks(c) * 100'000);
    }
    bank.onTrackEnd(4);
    // No hole in the timeline of track 4.
    for (size_t i = 1; i < audio.size(); ++i) CHECK(audio[i].pts - audio[i - 1].pts < seconds(0.03));

    packets.insert(packets.end(), audio.begin(), audio.end());
    std::stable_sort(packets.begin(), packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
    const auto dir = freshTempDir("bank_gap");
    for (const char* container : {"mp4", "mkv"}) {
      const auto file = dir / (std::string("gap.") + container);
      ClipWriter w(file, container, {v, info});
      for (const auto& p : packets) w.write(p);
      w.finish();
      const double tone = firstEventTime(file, AVMEDIA_TYPE_AUDIO);
      MESSAGE(container << ": tone at " << tone << " s");
      CHECK(tone == doctest::Approx(2.0).epsilon(0.03));
    }
  }
}
