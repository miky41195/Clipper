#include "session/Recorder.h"

namespace clipper {
namespace fs = std::filesystem;

Recorder::Recorder(PacketBus& bus, std::vector<TrackInfo> tracks, fs::path finalPath, std::string container,
                   std::function<void()> requestKeyframe, const std::string& app)
    : bus_(bus), finalPath_(std::move(finalPath)), container_(std::move(container)) {
  workPath_ = finalPath_;
  workPath_.replace_extension(L".rec.mkv");
  writer_ = std::make_unique<ClipWriter>(workPath_, "mkv", std::move(tracks));
  if (!app.empty()) writer_->setApp(app);
  thread_ = std::thread([this] { writerLoop(); });
  // Runs on encoder threads: only queue the packet (payloads are shared, not copied).
  subscription_ = bus_.subscribe([this](const Packet& p) {
    {
      std::lock_guard lk(mu_);
      if (draining_) return;
      queue_.push_back(p);
      if (p.track == kVideoTrack) {
        if (first_ < 0 && p.keyframe) first_ = p.pts;
        if (first_ >= 0) last_ = p.pts + p.duration;
      }
    }
    cv_.notify_one();
  });
  if (requestKeyframe) requestKeyframe();  // start without waiting up to a full GOP
}

Recorder::~Recorder() {
  try {
    stop();
  } catch (...) {
  }
  if (thread_.joinable()) thread_.join();
}

void Recorder::setWriteHookForTest(std::function<void()> hook) {
  std::lock_guard lk(mu_);
  writeHook_ = std::move(hook);
}

void Recorder::writerLoop() {
  while (true) {
    Packet p;
    std::function<void()> hook;
    {
      std::unique_lock lk(mu_);
      cv_.wait(lk, [&] { return draining_ || !queue_.empty(); });
      if (queue_.empty()) return;  // draining and everything written
      p = std::move(queue_.front());
      queue_.pop_front();
      hook = writeHook_;
    }
    if (hook) hook();
    try {
      writer_->write(p);
    } catch (...) {
      std::lock_guard lk(mu_);
      if (!writeError_) writeError_ = std::current_exception();
    }
  }
}

void Recorder::detach() {
  if (detached_) return;
  bus_.unsubscribe(subscription_);
  detached_ = true;
}

fs::path Recorder::stop() {
  detach();
  if (stopped_) return finalPath_;
  stopped_ = true;
  {
    std::lock_guard lk(mu_);
    draining_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  if (writeError_) std::rethrow_exception(writeError_);
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
