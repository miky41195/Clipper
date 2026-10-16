#include "encode/AudioEncoderBank.h"
#include <algorithm>
#include <vector>

namespace clipper {
namespace {
constexpr int kRate = 48000;
constexpr int64_t kFrameSamples = 1024;  // one AAC frame
Ticks samplesToTicks(int64_t n) { return n * kTicksPerSecond / kRate; }
}  // namespace

AudioEncoderBank::AudioEncoderBank(int kbps, AudioEncoder::PacketSink sink) : kbps_(kbps), sink_(std::move(sink)) {}

void AudioEncoderBank::emit(Packet p) {
  {
    std::lock_guard lk(mu_);
    Ticks& end = lastEnd_[p.track];
    end = std::max(end, p.pts + p.duration);
  }
  sink_(std::move(p));
}

Bytes AudioEncoderBank::silentFrame() {
  if (silent_) return silent_;
  std::vector<Packet> out;
  AudioEncoder enc({kRate, 2, kbps_}, 0, [&](Packet p) { out.push_back(std::move(p)); });
  std::vector<int16_t> zeros(size_t(kFrameSamples) * 2, 0);
  for (int i = 0; i < 6; ++i) enc.encode(zeros.data(), size_t(kFrameSamples), samplesToTicks(i * kFrameSamples));
  enc.flush();
  // A frame from the middle: steady-state silence, independent of encoder start-up.
  silent_ = out.size() > 2 ? out[out.size() / 2].data : (out.empty() ? nullptr : out.back().data);
  return silent_;
}

TrackInfo AudioEncoderBank::onTrack(const TrackInfo& t) {
  auto enc = std::make_unique<AudioEncoder>(AudioEncoderConfig{t.sampleRate, t.channels, kbps_}, t.id,
                                            [this](Packet p) { emit(std::move(p)); });
  TrackInfo out = t;
  out.extradata = enc->extradata();
  std::lock_guard lk(mu_);
  if (const auto it = lastEnd_.find(t.id); it != lastEnd_.end()) pendingFill_[t.id] = it->second;  // track came back
  enc_[t.id] = std::move(enc);
  return out;
}

void AudioEncoderBank::onChunk(int track, const int16_t* pcm, size_t frames, Ticks pts) {
  std::lock_guard lk(mu_);
  const auto it = enc_.find(track);
  if (it == enc_.end()) return;
  if (const auto fill = pendingFill_.find(track); fill != pendingFill_.end()) {
    const Ticks start = fill->second;
    pendingFill_.erase(fill);
    if (const Bytes silence = pts > start ? silentFrame() : nullptr) {
      const int64_t gapSamples = (pts - start) * kRate / kTicksPerSecond;
      for (int64_t s = 0; s < gapSamples; s += kFrameSamples) {
        // The last frame is shortened so the silence ends exactly where the new audio starts.
        const Ticks at = start + samplesToTicks(s);
        const Ticks dur = std::min(samplesToTicks(kFrameSamples), pts - at);
        if (dur > 0) emit(Packet{track, at, dur, true, silence});
      }
    }
  }
  it->second->encode(pcm, frames, pts);
}

void AudioEncoderBank::onTrackEnd(int track) {
  std::unique_ptr<AudioEncoder> enc;
  {
    std::lock_guard lk(mu_);
    if (auto it = enc_.find(track); it != enc_.end()) {
      enc = std::move(it->second);
      enc_.erase(it);
    }
  }
  if (enc) enc->flush();
}

}  // namespace clipper
