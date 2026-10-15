#include <doctest/doctest.h>
#include <mutex>
#include "MediaGen.h"

using namespace clipper;

TEST_CASE("isKeyframe detects H.264 IDR") {
  const uint8_t idr[] = {0, 0, 0, 1, 0x67, 0x42, 0, 0, 0, 1, 0x68, 0xCE, 0, 0, 0, 1, 0x65, 0x88};
  const uint8_t p[] = {0, 0, 0, 1, 0x41, 0x9A};
  CHECK(isKeyframe(Codec::H264, idr, sizeof idr));
  CHECK_FALSE(isKeyframe(Codec::H264, p, sizeof p));
}

TEST_CASE("isKeyframe detects HEVC IRAP") {
  const uint8_t idr[] = {0, 0, 0, 1, 0x40, 0x01, 0, 0, 1, 0x26, 0x01, 0xAF};  // VPS then IDR_W_RADL (19)
  const uint8_t trail[] = {0, 0, 1, 0x02, 0x01, 0xD0};                     // TRAIL_R (1)
  CHECK(isKeyframe(Codec::HEVC, idr, sizeof idr));
  CHECK_FALSE(isKeyframe(Codec::HEVC, trail, sizeof trail));
}

TEST_CASE("isKeyframe detects AV1 sequence header") {
  const uint8_t key[] = {0x12, 0x00, 0x0A, 0x02, 0x00, 0x00, 0x32, 0x01, 0x10};  // TD, SEQ(size 2), FRAME
  const uint8_t inter[] = {0x12, 0x00, 0x32, 0x02, 0x30, 0x00};                   // TD, FRAME
  CHECK(isKeyframe(Codec::AV1, key, sizeof key));
  CHECK_FALSE(isKeyframe(Codec::AV1, inter, sizeof inter));
}

TEST_SUITE("gpu") {
  TEST_CASE("hardware H.264 encodes with a keyframe every second") {
    const auto g = createGpuDevice();
    std::vector<Packet> out;
    std::mutex mu;
    VideoEncoder enc(g, {Codec::H264, 1280, 720, 60, 8000, false}, [&](Packet p) {
      std::lock_guard lk(mu);
      out.push_back(std::move(p));
    });
    CHECK(enc.hardware());
    MESSAGE("encoder: " << enc.name());
    std::vector<winrt::com_ptr<ID3D11Texture2D>> ring;
    for (int i = 0; i < 4; ++i) ring.push_back(makeSolidNv12(g, 1280, 720, uint8_t(30 + i * 50)));
    for (int i = 0; i < 180; ++i) enc.encode(ring[i % 4].get(), Ticks(i) * kTicksPerSecond / 60);
    enc.flush();
    std::lock_guard lk(mu);
    CHECK(out.size() >= 170);
    REQUIRE(!out.empty());
    CHECK(out.front().keyframe);
    int keys = 0;
    for (size_t i = 0; i < out.size(); ++i) {
      keys += out[i].keyframe ? 1 : 0;
      if (i) CHECK(out[i].pts > out[i - 1].pts);
    }
    CHECK(keys >= 3);
    CHECK(keys <= 4);
    CHECK(enc.trackInfo().width == 1280);
  }

  TEST_CASE("software H.264 encoder works") {
    const auto g = createGpuDevice();
    const auto pk = encodeTestVideo(g, {Codec::H264, 640, 360, 30, 2000, true}, 1.0, 0);
    CHECK(pk.size() >= 25);
    REQUIRE(!pk.empty());
    CHECK(pk.front().keyframe);
  }

  TEST_CASE("AV1 hardware when available") {
    const auto g = createGpuDevice();
    try {
      VideoEncoder enc(g, {Codec::AV1, 1280, 720, 60, 8000, false}, [](Packet) {});
      MESSAGE("AV1 encoder: " << enc.name());
      const auto pk = encodeTestVideo(g, {Codec::AV1, 1280, 720, 60, 8000, false}, 1.0, 0);
      CHECK(pk.size() >= 50);
      REQUIRE(!pk.empty());
      CHECK(pk.front().keyframe);
    } catch (const std::exception& e) {
      MESSAGE("AV1 not available: " << e.what());
    }
  }

  TEST_CASE("fallback chain always yields an encoder") {
    const auto g = createGpuDevice();
    std::string warning;
    auto enc = createVideoEncoderWithFallback(g, {Codec::AV1, 1920, 1080, 60, 20000, false}, [](Packet) {}, &warning);
    CHECK(enc != nullptr);
    MESSAGE("chosen: " << enc->name() << " warning: " << warning);
  }
}
