#include "capture/VideoPipeline.h"
#include "capture/Monitors.h"
#include "core/SizeEstimate.h"
#include <format>

namespace clipper {

VideoPipeline::VideoPipeline(GpuDevice g, VideoPipelineConfig cfg, PacketSink onPacket, TrackSink onTrack,
                             WarnSink onWarning)
    : g_(std::move(g)),
      cfg_(cfg),
      onPacket_(std::move(onPacket)),
      onTrack_(std::move(onTrack)),
      onWarning_(std::move(onWarning)) {
  build();  // errors surface to the caller here
  thread_ = std::thread([this] { run(); });
}

VideoPipeline::~VideoPipeline() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void VideoPipeline::requestKeyframe() { keyRequested_ = true; }

void VideoPipeline::build() {
  if (encoder_) encoder_->flush();
  encoder_.reset();
  scaler_.reset();
  capture_.reset();
  ring_.clear();
  capture_ = std::make_unique<ScreenCapture>(g_, cfg_.monitor, cfg_.cursor);
  const auto [w, h] = computeOutputSize(capture_->width(), capture_->height(), cfg_.outputHeight);
  scaler_ = std::make_unique<FrameScaler>(g_, capture_->width(), capture_->height(), w, h);
  for (int i = 0; i < 6; ++i) ring_.push_back(createNv12Texture(g_, w, h));
  const int kbps = cfg_.quality == Quality::Custom ? cfg_.customKbps : presetVideoKbps(cfg_.quality, cfg_.codec, w, h, cfg_.fps);
  std::string warning;
  encoder_ = createVideoEncoderWithFallback(g_, {cfg_.codec, w, h, cfg_.fps, kbps, false}, onPacket_, &warning);
  if (!warning.empty() && onWarning_) onWarning_(warning);
  onTrack_(encoder_->trackInfo());
}

void VideoPipeline::run() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const Ticks period = kTicksPerSecond / cfg_.fps;
  Ticks next = now();
  while (!stop_) {
    next += period;
    const Ticks wait = next - now();
    if (wait > 0) {
      LARGE_INTEGER due;
      due.QuadPart = -wait;
      SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
      WaitForSingleObject(timer, 1000);
    } else if (wait < -4 * period) {
      next = now();  // fell behind (system stall): resync instead of bursting
    }
    try {
      if (capture_->sizeChanged()) {
        build();
        continue;
      }
      ID3D11Texture2D* out = ring_[ringIndex_++ % ring_.size()].get();
      if (!capture_->convertLatest(*scaler_, out)) continue;
      if (keyRequested_.exchange(false)) encoder_->requestKeyframe();
      encoder_->encode(out, next);
    } catch (const std::exception& e) {
      if (onWarning_) onWarning_(std::format("Errore cattura video: {}", e.what()));
      Sleep(500);
    } catch (const winrt::hresult_error& e) {
      if (onWarning_) onWarning_("Errore cattura video: " + winrt::to_string(e.message()));
      Sleep(500);
    }
  }
  try {
    if (encoder_) encoder_->flush();
  } catch (...) {
  }
  encoder_.reset();
  scaler_.reset();
  capture_.reset();
  if (timer) CloseHandle(timer);
  CoUninitialize();
}

}  // namespace clipper
