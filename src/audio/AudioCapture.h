#pragma once
#include "audio/AudioHub.h"
#include "audio/Wasapi.h"
#include <condition_variable>
#include <map>
#include <set>

namespace clipper {

struct AudioCaptureConfig {
  std::vector<std::string> blacklist;  // lowercase exe names
  std::string micDeviceId;
  bool mic = true;
};

// Polls audio sessions every second and keeps one hub track per app (exe), plus the microphone.
class AudioCapture {
public:
  using WarnSink = std::function<void(const std::string&)>;
  AudioCapture(AudioHub& hub, AudioCaptureConfig cfg, WarnSink onWarning);
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  void refresh();

private:
  struct App {
    int track = -1;
    Ticks lastSeen = 0;
    std::map<uint32_t, std::pair<std::unique_ptr<LoopbackCapture>, std::shared_ptr<PcmSource>>> captures;
  };
  void dropApp(App& app);

  AudioHub& hub_;
  AudioCaptureConfig cfg_;
  WarnSink onWarning_;
  std::map<std::string, App> apps_;
  std::set<uint32_t> failedPids_;
  int micTrack_ = -1;
  std::unique_ptr<MicCapture> mic_;
  std::shared_ptr<PcmSource> micSource_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace clipper
