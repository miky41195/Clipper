#pragma once
#include "audio/AudioCapture.h"
#include "audio/AudioHub.h"
#include "buffer/ReplayBuffer.h"
#include "capture/VideoPipeline.h"
#include "core/Settings.h"
#include "encode/AudioEncoderBank.h"
#include "session/PacketBus.h"
#include <map>

namespace clipper {

BufferConfig bufferConfigFor(const Settings& s, const std::filesystem::path& dir);

// Everything that runs while Clipper is capturing: video pipeline, audio capture + hub + encoders,
// replay buffer and the packet bus used by the recorder. Destroying it stops capture.
class CaptureSession {
public:
  using WarnSink = std::function<void(const std::string&)>;
  CaptureSession(const Settings& s, WarnSink onWarning, GpuDevice gpu = {});  // empty gpu = create one
  ~CaptureSession();
  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;

  ReplayBuffer::Snapshot snapshot(Ticks length) const { return buffer_.snapshot(length); }
  std::vector<TrackInfo> tracks() const;  // live tracks, video first
  PacketBus& bus() { return bus_; }
  AudioHub& hub() { return hub_; }
  void requestKeyframe();
  size_t bufferRamBytes() const { return buffer_.ramBytes(); }

private:
  void shutdown();
  void onPacket(Packet p);
  void onTrack(const TrackInfo& t);
  void onTrackEnd(int id);

  WarnSink onWarning_;
  ReplayBuffer buffer_;
  PacketBus bus_;
  AudioEncoderBank bank_;
  mutable std::mutex trackMu_;
  std::map<int, TrackInfo> tracks_;
  AudioHub hub_;
  std::atomic<bool> stopTick_{false};
  std::thread tickThread_;
  std::unique_ptr<AudioCapture> audio_;
  std::unique_ptr<VideoPipeline> video_;
};

}  // namespace clipper
