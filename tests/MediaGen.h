#pragma once
#include "encode/VideoEncoder.h"
#include <algorithm>
#include <functional>
#include <mutex>
#include <vector>

// Solid NV12 frame: Y = luma, UV = 128.
inline winrt::com_ptr<ID3D11Texture2D> makeSolidNv12(const clipper::GpuDevice& g, int w, int h, uint8_t luma) {
  std::vector<uint8_t> data(size_t(w) * h * 3 / 2, 128);
  std::fill(data.begin(), data.begin() + size_t(w) * h, luma);
  return clipper::createNv12Texture(g, w, h, data.data());
}

// Encodes `seconds` of video at fps; lumaOf(frameIndex) chooses each frame's brightness.
inline std::vector<clipper::Packet> encodeTestVideo(const clipper::GpuDevice& g, clipper::VideoEncoderConfig cfg,
                                                    double secs, clipper::Ticks startPts,
                                                    std::function<uint8_t(int)> lumaOf = nullptr,
                                                    clipper::TrackInfo* info = nullptr) {
  std::vector<clipper::Packet> out;
  std::mutex mu;
  std::string warning;
  auto enc = clipper::createVideoEncoderWithFallback(
      g, cfg, [&](clipper::Packet p) { std::lock_guard lk(mu); out.push_back(std::move(p)); }, &warning);
  const int frames = int(secs * cfg.fps);
  std::vector<winrt::com_ptr<ID3D11Texture2D>> keep;
  for (int i = 0; i < frames; ++i) {
    keep.push_back(makeSolidNv12(g, cfg.width, cfg.height, lumaOf ? lumaOf(i) : uint8_t(16 + (i * 7) % 200)));
    enc->encode(keep.back().get(), startPts + clipper::Ticks(i) * clipper::kTicksPerSecond / cfg.fps);
  }
  enc->flush();
  if (info) *info = enc->trackInfo();
  enc.reset();
  std::lock_guard lk(mu);
  return out;
}
