#include "session/CaptureSession.h"
#include "capture/Monitors.h"
#include "core/Paths.h"
#include "core/SizeEstimate.h"
#include <algorithm>

namespace clipper {
namespace {
constexpr Ticks kAudioLatency = 60 * 10'000;  // hub runs 60 ms behind real time so late packets still align
}

BufferConfig bufferConfigFor(const Settings& s, const std::filesystem::path& dir) {
  BufferConfig c;
  c.duration = seconds(s.replaySeconds);
  c.spillToDisk = s.replaySeconds > s.ramThresholdSeconds;
  c.segmentLength = seconds(c.spillToDisk ? kDiskSegmentSeconds : kRamSegmentSeconds);
  c.dir = dir;
  c.fallbackDuration = seconds(std::max(10, s.ramThresholdSeconds));
  return c;
}

CaptureSession::CaptureSession(const Settings& s, WarnSink onWarning, GpuDevice gpu)
    : onWarning_(std::move(onWarning)),
      buffer_(bufferConfigFor(s, bufferDir(s)), onWarning_),
      bank_(s.audioKbps, [this](Packet p) { onPacket(std::move(p)); }),
      hub_(
          AudioHubConfig{48000, 2, 480, s.mixTrack},
          [this](int t, const int16_t* pcm, size_t f, Ticks pts) { bank_.onChunk(t, pcm, f, pts); },
          [this](const TrackInfo& t) { onTrack(bank_.onTrack(t)); }, [this](int id) { onTrackEnd(id); }) {
  hub_.start(now() - kAudioLatency);
  tickThread_ = std::thread([this] {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (!stopTick_) {
      hub_.tick(now() - kAudioLatency);
      Sleep(10);
    }
    CoUninitialize();
  });
  try {
    audio_ = std::make_unique<AudioCapture>(hub_, AudioCaptureConfig{s.audioBlacklist, s.micDeviceId, s.captureMic},
                                            onWarning_);
    const auto monitors = listMonitors();
    const MonitorInfo* mon = pickMonitor(monitors, s.monitorIndex);
    if (!mon) throw std::runtime_error("nessun monitor trovato");
    if (mon->index != s.monitorIndex && onWarning_) onWarning_("Monitor configurato non trovato: uso il principale");
    video_ = std::make_unique<VideoPipeline>(
        gpu.device ? gpu : createGpuDevice(),
        VideoPipelineConfig{mon->handle, s.outputHeight, s.fps, s.codec, s.quality, s.customKbps, s.captureCursor},
        [this](Packet p) { onPacket(std::move(p)); }, [this](const TrackInfo& t) { onTrack(t); }, onWarning_);
  } catch (...) {
    shutdown();  // the destructor does not run for a half-built object; a joinable thread would terminate
    throw;
  }
}

CaptureSession::~CaptureSession() { shutdown(); }

void CaptureSession::shutdown() {
  video_.reset();
  audio_.reset();
  stopTick_ = true;
  if (tickThread_.joinable()) tickThread_.join();
  for (const auto& t : hub_.tracks()) bank_.onTrackEnd(t.id);
}

void CaptureSession::onPacket(Packet p) {
  bus_.publish(p);
  buffer_.push(std::move(p));
}

void CaptureSession::onTrack(const TrackInfo& t) {
  buffer_.setTrack(t);
  std::lock_guard lk(trackMu_);
  tracks_[t.id] = t;
}

void CaptureSession::onTrackEnd(int id) {
  bank_.onTrackEnd(id);
  buffer_.endTrack(id);
  std::lock_guard lk(trackMu_);
  tracks_.erase(id);
}

std::vector<TrackInfo> CaptureSession::tracks() const {
  std::lock_guard lk(trackMu_);
  std::vector<TrackInfo> out;
  for (const auto& [id, t] : tracks_) out.push_back(t);
  return out;
}

void CaptureSession::requestKeyframe() {
  if (video_) video_->requestKeyframe();
}

}  // namespace clipper
