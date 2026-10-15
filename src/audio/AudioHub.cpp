#include "audio/AudioHub.h"
#include <algorithm>

namespace clipper {
namespace {
int16_t clamp16(int32_t v) { return int16_t(std::clamp(v, -32768, 32767)); }
}  // namespace

AudioHub::AudioHub(AudioHubConfig cfg, ChunkSink onChunk, TrackSink onTrack, EndSink onEnd)
    : cfg_(cfg), onChunk_(std::move(onChunk)), onTrack_(std::move(onTrack)), onEnd_(std::move(onEnd)) {
  const size_t n = cfg_.chunkFrames * size_t(cfg_.channels);
  acc_.resize(n);
  mix_.resize(n);
  buf_.resize(n);
  tmp_.resize(n);
  if (cfg_.mixTrack) {
    std::lock_guard lk(mu_);
    mixId_ = addTrackLocked("Mix");
  }
}

int AudioHub::addTrackLocked(const std::string& name) {
  Track t;
  t.info.id = nextId_++;
  t.info.kind = TrackKind::Audio;
  t.info.codec = Codec::AAC;
  t.info.name = name;
  t.info.sampleRate = cfg_.sampleRate;
  t.info.channels = cfg_.channels;
  const TrackInfo info = t.info;
  tracks_.emplace(info.id, std::move(t));
  if (onTrack_) onTrack_(info);
  return info.id;
}

void AudioHub::start(Ticks origin) {
  std::lock_guard lk(mu_);
  origin_ = origin;
  chunkIndex_ = 0;
  started_ = true;
}

int AudioHub::addTrack(const std::string& name) {
  std::lock_guard lk(mu_);
  return addTrackLocked(name);
}

void AudioHub::removeTrack(int id) {
  std::lock_guard lk(mu_);
  if (tracks_.erase(id) == 0) return;
  if (id == othersId_) othersId_ = -1;
  if (onEnd_) onEnd_(id);
}

void AudioHub::attach(int id, std::shared_ptr<PcmSource> src) {
  std::lock_guard lk(mu_);
  if (auto it = tracks_.find(id); it != tracks_.end()) it->second.sources.push_back(std::move(src));
}

void AudioHub::detach(int id, const std::shared_ptr<PcmSource>& src) {
  std::lock_guard lk(mu_);
  if (auto it = tracks_.find(id); it != tracks_.end()) std::erase(it->second.sources, src);
}

int AudioHub::trackForNewApp(const std::string& name) {
  std::lock_guard lk(mu_);
  if (!pinned_) return addTrackLocked(name);
  if (othersId_ < 0) othersId_ = addTrackLocked("Altre app");
  return othersId_;
}

void AudioHub::pin() {
  std::lock_guard lk(mu_);
  pinned_ = true;
}

void AudioHub::unpin() {
  std::lock_guard lk(mu_);
  pinned_ = false;
}

bool AudioHub::pinned() const {
  std::lock_guard lk(mu_);
  return pinned_;
}

int AudioHub::othersTrackId() const {
  std::lock_guard lk(mu_);
  return othersId_;
}

void AudioHub::tick(Ticks now) {
  std::lock_guard lk(mu_);
  if (!started_) return;
  const size_t n = cfg_.chunkFrames;
  const size_t samples = n * size_t(cfg_.channels);
  while (true) {
    const Ticks pts = origin_ + Ticks(chunkIndex_ * n * uint64_t(kTicksPerSecond) / uint64_t(cfg_.sampleRate));
    const Ticks end = origin_ + Ticks((chunkIndex_ + 1) * n * uint64_t(kTicksPerSecond) / uint64_t(cfg_.sampleRate));
    if (end > now) break;
    std::fill(mix_.begin(), mix_.end(), 0);
    for (auto& [id, t] : tracks_) {
      if (id == mixId_) continue;
      std::fill(acc_.begin(), acc_.end(), 0);
      for (auto& s : t.sources) {
        if (s->pullAt(tmp_.data(), n, pts) == 0) continue;
        for (size_t i = 0; i < samples; ++i) acc_[i] += tmp_[i];
      }
      for (size_t i = 0; i < samples; ++i) {
        buf_[i] = clamp16(acc_[i]);
        mix_[i] += acc_[i];
      }
      onChunk_(id, buf_.data(), n, pts);
    }
    if (mixId_ >= 0) {
      for (size_t i = 0; i < samples; ++i) buf_[i] = clamp16(mix_[i]);
      onChunk_(mixId_, buf_.data(), n, pts);
    }
    ++chunkIndex_;
  }
}

std::vector<TrackInfo> AudioHub::tracks() const {
  std::lock_guard lk(mu_);
  std::vector<TrackInfo> out;
  for (const auto& [id, t] : tracks_) out.push_back(t.info);
  return out;
}

}  // namespace clipper
