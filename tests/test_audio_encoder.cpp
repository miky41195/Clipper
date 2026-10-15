#include <doctest/doctest.h>
#include <cmath>
#include <numbers>
#include <vector>
#include "encode/AudioEncoder.h"

using namespace clipper;

// Interleaved stereo int16 sine.
inline std::vector<int16_t> makeSine(int rate, double hz, double secs, double amp = 0.5) {
  const size_t frames = size_t(rate * secs);
  std::vector<int16_t> out(frames * 2);
  for (size_t i = 0; i < frames; ++i) {
    const auto v = int16_t(amp * 32767 * std::sin(2 * std::numbers::pi * hz * double(i) / rate));
    out[i * 2] = out[i * 2 + 1] = v;
  }
  return out;
}

TEST_SUITE("gpu") {
  TEST_CASE("AAC encoder produces packets with AudioSpecificConfig") {
    std::vector<Packet> out;
    AudioEncoder enc({48000, 2, 160}, 3, [&](Packet p) { out.push_back(std::move(p)); });
    const auto sine = makeSine(48000, 440, 1.0);
    for (size_t i = 0; i < 100; ++i) enc.encode(sine.data() + i * 480 * 2, 480, seconds(1) + Ticks(i) * 100'000);
    enc.flush();
    CHECK(enc.extradata() == std::vector<uint8_t>{0x11, 0x90});
    CHECK(out.size() >= 45);
    CHECK(out.size() <= 48);
    for (size_t i = 0; i < out.size(); ++i) {
      CHECK(out[i].track == 3);
      CHECK(out[i].data->size() > 0);
      if (i) CHECK(out[i].pts >= out[i - 1].pts);
    }
  }
}
