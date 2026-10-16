#pragma once
#include "core/Clock.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace clipper {

enum class Codec { H264, HEVC, AV1, AAC };
enum class Quality { Low, Medium, High, Custom };
enum class TrackKind { Video, Audio };

constexpr int kVideoTrack = 0;

struct TrackInfo {
  int id = 0;
  TrackKind kind = TrackKind::Audio;
  Codec codec = Codec::AAC;
  std::string name;  // "Video", "Mix", "Microfono", "Altre app" or the app display name
  int width = 0;
  int height = 0;
  int fps = 0;
  int sampleRate = 48000;
  int channels = 2;
  std::vector<uint8_t> extradata;
};

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

inline Bytes makeBytes(const uint8_t* p, size_t n) {
  return std::make_shared<const std::vector<uint8_t>>(p, p + n);
}

constexpr Ticks kNoDts = INT64_MIN;

struct Packet {
  int track = 0;
  Ticks pts = 0;
  Ticks duration = 0;
  bool keyframe = false;
  Bytes data;
  Ticks dts = kNoDts;  // decode time when it differs from pts (B-frames); Clipper's own encoders never set it
};

}  // namespace clipper
