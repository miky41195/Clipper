#pragma once
#include "core/Media.h"
#include "gpu/GpuDevice.h"
#include <functional>
#include <memory>
#include <string>

namespace clipper {

struct VideoEncoderConfig {
  Codec codec = Codec::H264;
  int width = 1920;
  int height = 1080;
  int fps = 60;
  int kbps = 25000;
  bool forceSoftware = false;
};

bool isKeyframe(Codec codec, const uint8_t* data, size_t size);
std::string codecName(Codec codec);

class VideoEncoder {
public:
  using PacketSink = std::function<void(Packet)>;
  VideoEncoder(const GpuDevice& gpu, const VideoEncoderConfig& cfg, PacketSink sink);
  ~VideoEncoder();
  VideoEncoder(const VideoEncoder&) = delete;
  VideoEncoder& operator=(const VideoEncoder&) = delete;

  // Encodes an NV12 texture of cfg.width x cfg.height. Drops the frame if the encoder is busy for >50 ms.
  void encode(ID3D11Texture2D* nv12, Ticks pts);
  void requestKeyframe();
  void flush();  // end of stream: drains pending output; encode() must not be called afterwards
  bool hardware() const;
  std::string name() const;
  TrackInfo trackInfo() const;

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<VideoEncoder> createVideoEncoderWithFallback(const GpuDevice& gpu, VideoEncoderConfig cfg,
                                                             VideoEncoder::PacketSink sink, std::string* warning);

}  // namespace clipper
