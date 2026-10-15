#include "buffer/ReplayBuffer.h"
#include <algorithm>
#include <format>

namespace clipper {

void ReplayBuffer::Snapshot::forEach(const std::function<void(const Packet&)>& fn) const {
  for (const auto& seg : segments)
    seg->forEach([&](const Packet& p) {
      if (p.pts >= start && p.pts <= end) fn(p);
    });
}

ReplayBuffer::ReplayBuffer(BufferConfig cfg, std::function<void(const std::string&)> onWarning)
    : cfg_(std::move(cfg)), onWarning_(std::move(onWarning)) {}

ReplayBuffer::~ReplayBuffer() { clear(); }

void ReplayBuffer::setTrack(const TrackInfo& info) {
  std::lock_guard lk(mu_);
  const auto it = tracks_.find(info.id);
  if (info.kind == TrackKind::Video && it != tracks_.end() &&
      (it->second.width != info.width || it->second.height != info.height || it->second.codec != info.codec))
    segments_.clear();
  tracks_[info.id] = info;
  ended_.erase(info.id);
}

void ReplayBuffer::endTrack(int id) {
  std::lock_guard lk(mu_);
  ended_.insert(id);
  pruneEndedLocked();
}

void ReplayBuffer::push(Packet p) {
  std::shared_ptr<Segment> closed;
  {
    std::lock_guard lk(mu_);
    const bool videoKey = p.track == kVideoTrack && p.keyframe;
    if (segments_.empty()) {
      if (!videoKey) return;
      segments_.push_back(std::make_shared<Segment>(p.pts));
    } else if (videoKey && p.pts - segments_.back()->start() >= cfg_.segmentLength) {
      closed = segments_.back();
      segments_.push_back(std::make_shared<Segment>(p.pts));
    }
    lastPts_ = std::max(lastPts_, p.pts);
    segments_.back()->add(std::move(p));
    evictLocked();
  }
  if (closed && spillEnabled_) enqueueSpill(std::move(closed));
}

void ReplayBuffer::evictLocked() {
  while (segments_.size() >= 2 && lastPts_ - segments_[1]->start() >= cfg_.duration) segments_.pop_front();
  pruneEndedLocked();
}

void ReplayBuffer::pruneEndedLocked() {
  for (auto it = ended_.begin(); it != ended_.end();) {
    const int id = *it;
    const bool used = std::any_of(segments_.begin(), segments_.end(), [&](const auto& s) { return s->hasTrack(id); });
    if (used) {
      ++it;
    } else {
      tracks_.erase(id);
      it = ended_.erase(it);
    }
  }
}

ReplayBuffer::Snapshot ReplayBuffer::snapshot(Ticks length) const {
  std::lock_guard lk(mu_);
  Snapshot s;
  if (segments_.empty()) return s;
  s.end = lastPts_;
  const Ticks target = lastPts_ - length;
  size_t idx = 0;
  for (size_t i = 0; i < segments_.size(); ++i)
    if (segments_[i]->start() <= target) idx = i;
  s.start = segments_[idx]->start();
  std::set<int> ids;
  // Include the previous segment too: late audio packets for [start, ...) may have landed there.
  for (size_t i = idx > 0 ? idx - 1 : 0; i < segments_.size(); ++i) {
    s.segments.push_back(segments_[i]);
    if (i >= idx)
      for (int id : segments_[i]->trackIds()) ids.insert(id);
  }
  for (int id : ids)
    if (const auto it = tracks_.find(id); it != tracks_.end()) s.tracks.push_back(it->second);
  return s;
}

Ticks ReplayBuffer::bufferedDuration() const {
  std::lock_guard lk(mu_);
  return segments_.empty() ? 0 : lastPts_ - segments_.front()->start();
}

size_t ReplayBuffer::ramBytes() const {
  std::lock_guard lk(mu_);
  size_t total = 0;
  for (const auto& s : segments_) total += s->ramBytes();
  return total;
}

void ReplayBuffer::clear() {
  std::lock_guard lk(mu_);
  segments_.clear();
  pruneEndedLocked();
}

void ReplayBuffer::waitIdle() {}

void ReplayBuffer::enqueueSpill(std::shared_ptr<Segment>) {}

void ReplayBuffer::writerLoop() {}

}  // namespace clipper
