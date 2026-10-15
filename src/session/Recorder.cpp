#include "session/Recorder.h"

namespace clipper {
namespace fs = std::filesystem;

Recorder::Recorder(PacketBus& bus, std::vector<TrackInfo> tracks, fs::path finalPath, std::string container,
                   std::function<void()> requestKeyframe)
    : bus_(bus), finalPath_(std::move(finalPath)), container_(std::move(container)) {
  workPath_ = finalPath_;
  workPath_.replace_extension(L".rec.mkv");
  writer_ = std::make_unique<ClipWriter>(workPath_, "mkv", std::move(tracks));
  subscription_ = bus_.subscribe([this](const Packet& p) {
    std::lock_guard lk(mu_);
    if (stopped_) return;
    writer_->write(p);
    if (p.track == kVideoTrack) {
      if (first_ < 0 && p.keyframe) first_ = p.pts;
      if (first_ >= 0) last_ = p.pts + p.duration;
    }
  });
  if (requestKeyframe) requestKeyframe();  // start without waiting up to a full GOP
}

Recorder::~Recorder() {
  try {
    stop();
  } catch (...) {
  }
}

void Recorder::detach() {
  if (detached_) return;
  bus_.unsubscribe(subscription_);
  detached_ = true;
}

fs::path Recorder::stop() {
  detach();
  std::lock_guard lk(mu_);
  if (stopped_) return finalPath_;
  stopped_ = true;
  writer_->finish();
  writer_.reset();
  if (container_ == "mkv") {
    fs::rename(workPath_, finalPath_);
  } else {
    remux(workPath_, finalPath_);
    std::error_code ec;
    fs::remove(workPath_, ec);
  }
  return finalPath_;
}

Ticks Recorder::duration() const {
  std::lock_guard lk(mu_);
  return first_ < 0 ? 0 : last_ - first_;
}

}  // namespace clipper
