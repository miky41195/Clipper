#include "audio/PcmSource.h"
#include <algorithm>

namespace clipper {
namespace {
constexpr Ticks kTolerance = 20'000;  // 2 ms: jitter below this is ignored
}

PcmSource::PcmSource(int sampleRate, int channels, size_t maxFrames)
    : rate_(sampleRate), ch_(channels), maxFrames_(maxFrames) {}

void PcmSource::dropFrontLocked(size_t frames) {
  q_.erase(q_.begin(), q_.begin() + std::ptrdiff_t(frames * size_t(ch_)));
  headTs_ += double(frames) * kTicksPerSecond / rate_;
}

void PcmSource::push(const int16_t* data, size_t frames, Ticks ts) {
  std::lock_guard lk(mu_);
  const size_t buffered = q_.size() / size_t(ch_);
  if (buffered == 0) {
    headTs_ = double(ts);
  } else {
    const double expected = headTs_ + double(buffered) * kTicksPerSecond / rate_;
    const double gap = double(ts) - expected;
    if (gap > kTolerance) {
      const size_t zeros = std::min(ticksToFrames(Ticks(gap)), maxFrames_);
      q_.insert(q_.end(), zeros * size_t(ch_), int16_t{0});
    }
  }
  q_.insert(q_.end(), data, data + frames * size_t(ch_));
  const size_t total = q_.size() / size_t(ch_);
  if (total > maxFrames_) dropFrontLocked(total - maxFrames_);
}

size_t PcmSource::pullAt(int16_t* out, size_t frames, Ticks pts) {
  std::fill_n(out, frames * size_t(ch_), int16_t{0});
  std::lock_guard lk(mu_);
  size_t buffered = q_.size() / size_t(ch_);
  if (buffered == 0) return 0;
  if (headTs_ < double(pts - kTolerance)) {
    const size_t drop = std::min(buffered, ticksToFrames(Ticks(double(pts) - headTs_)));
    dropFrontLocked(drop);
    buffered -= drop;
    if (buffered == 0) return 0;
  }
  size_t offset = 0;
  if (headTs_ > double(pts + kTolerance)) {
    offset = ticksToFrames(Ticks(headTs_ - double(pts)));
    if (offset >= frames) return 0;
  }
  const size_t n = std::min(frames - offset, buffered);
  std::copy_n(q_.begin(), n * size_t(ch_), out + offset * size_t(ch_));
  dropFrontLocked(n);
  return n;
}

size_t PcmSource::bufferedFrames() const {
  std::lock_guard lk(mu_);
  return q_.size() / size_t(ch_);
}

Ticks PcmSource::headTimestamp() const {
  std::lock_guard lk(mu_);
  return Ticks(headTs_ + 0.5);
}

}  // namespace clipper
