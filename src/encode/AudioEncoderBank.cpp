#include "encode/AudioEncoderBank.h"

namespace clipper {

AudioEncoderBank::AudioEncoderBank(int kbps, AudioEncoder::PacketSink sink) : kbps_(kbps), sink_(std::move(sink)) {}

TrackInfo AudioEncoderBank::onTrack(const TrackInfo& t) {
  auto enc = std::make_unique<AudioEncoder>(AudioEncoderConfig{t.sampleRate, t.channels, kbps_}, t.id, sink_);
  TrackInfo out = t;
  out.extradata = enc->extradata();
  std::lock_guard lk(mu_);
  enc_[t.id] = std::move(enc);
  return out;
}

void AudioEncoderBank::onChunk(int track, const int16_t* pcm, size_t frames, Ticks pts) {
  std::lock_guard lk(mu_);
  if (auto it = enc_.find(track); it != enc_.end()) it->second->encode(pcm, frames, pts);
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
