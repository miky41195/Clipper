#pragma once
#include <atomic>
#include <functional>
#include <thread>

namespace clipper {

// Shared-mode WASAPI playback on the default device: 48 kHz stereo float, pulled from `fill`
// on a dedicated thread. Throws if no output device can be opened.
class AudioOut {
public:
  using Fill = std::function<void(float* interleaved, size_t frames)>;
  explicit AudioOut(Fill fill);
  ~AudioOut();
  AudioOut(const AudioOut&) = delete;
  AudioOut& operator=(const AudioOut&) = delete;

private:
  Fill fill_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace clipper
