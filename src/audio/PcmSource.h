#pragma once
#include "core/Clock.h"
#include <cstdint>
#include <deque>
#include <mutex>

namespace clipper {

// Thread-safe interleaved int16 FIFO that knows the timestamp of its first frame.
// Capture threads push(); the hub pulls exactly the frames belonging to [pts, pts + frames).
class PcmSource {
public:
  explicit PcmSource(int sampleRate = 48000, int channels = 2, size_t maxFrames = 48000);

  void push(const int16_t* data, size_t frames, Ticks ts);
  // Fills out[frames * channels]; missing audio is silence. Returns the number of real frames copied.
  size_t pullAt(int16_t* out, size_t frames, Ticks pts);
  size_t bufferedFrames() const;
  Ticks headTimestamp() const;

private:
  Ticks framesToTicks(size_t f) const { return Ticks(double(f) * kTicksPerSecond / rate_); }
  size_t ticksToFrames(Ticks t) const { return t <= 0 ? 0 : size_t(double(t) * rate_ / kTicksPerSecond + 0.5); }
  void dropFrontLocked(size_t frames);

  const int rate_;
  const int ch_;
  const size_t maxFrames_;
  mutable std::mutex mu_;
  std::deque<int16_t> q_;
  double headTs_ = 0;  // double: avoids drift from rounding frame -> tick conversions
};

}  // namespace clipper
