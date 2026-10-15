#pragma once
#include "capture/FrameScaler.h"
#include "capture/ScreenCapture.h"
#include "encode/VideoEncoder.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace clipper {

struct VideoPipelineConfig {
  HMONITOR monitor = nullptr;
  int outputHeight = 0;
  int fps = 60;
  Codec codec = Codec::H264;
  Quality quality = Quality::High;
  int customKbps = 25000;
  bool cursor = true;
};

// Pacer thread: every 1/fps scales the latest captured frame to NV12 and encodes it (constant frame rate).
class VideoPipeline {
public:
  using PacketSink = std::function<void(Packet)>;
  using TrackSink = std::function<void(const TrackInfo&)>;
  using WarnSink = std::function<void(const std::string&)>;

  VideoPipeline(GpuDevice g, VideoPipelineConfig cfg, PacketSink onPacket, TrackSink onTrack, WarnSink onWarning);
  ~VideoPipeline();
  VideoPipeline(const VideoPipeline&) = delete;
  VideoPipeline& operator=(const VideoPipeline&) = delete;

  void requestKeyframe();

private:
  void build();
  void run();

  GpuDevice g_;
  VideoPipelineConfig cfg_;
  PacketSink onPacket_;
  TrackSink onTrack_;
  WarnSink onWarning_;
  std::unique_ptr<ScreenCapture> capture_;
  std::unique_ptr<FrameScaler> scaler_;
  std::unique_ptr<VideoEncoder> encoder_;
  std::vector<winrt::com_ptr<ID3D11Texture2D>> ring_;
  size_t ringIndex_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<bool> keyRequested_{false};
  std::thread thread_;
};

}  // namespace clipper
