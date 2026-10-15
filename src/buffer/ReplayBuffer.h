#pragma once
#include "buffer/Segment.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace clipper {

struct BufferConfig {
  Ticks duration = seconds(60);
  Ticks segmentLength = seconds(1);
  bool spillToDisk = false;
  std::filesystem::path dir;
  Ticks fallbackDuration = seconds(120);  // used if disk spill fails
};

class ReplayBuffer {
public:
  struct Snapshot {
    std::vector<TrackInfo> tracks;  // video first
    Ticks start = 0;
    Ticks end = 0;
    std::vector<std::shared_ptr<const Segment>> segments;
    void forEach(const std::function<void(const Packet&)>& fn) const;
    bool empty() const { return segments.empty(); }
  };

  explicit ReplayBuffer(BufferConfig cfg, std::function<void(const std::string&)> onWarning = {});
  ~ReplayBuffer();
  ReplayBuffer(const ReplayBuffer&) = delete;
  ReplayBuffer& operator=(const ReplayBuffer&) = delete;

  void setTrack(const TrackInfo& info);
  void endTrack(int id);
  void push(Packet p);
  Snapshot snapshot(Ticks length) const;
  Ticks bufferedDuration() const;
  size_t ramBytes() const;
  void clear();
  void waitIdle();

private:
  void evictLocked();
  void pruneEndedLocked();
  void enqueueSpill(std::shared_ptr<Segment> seg);
  void writerLoop();

  BufferConfig cfg_;
  std::function<void(const std::string&)> onWarning_;

  mutable std::mutex mu_;
  std::deque<std::shared_ptr<Segment>> segments_;
  std::map<int, TrackInfo> tracks_;
  std::set<int> ended_;
  Ticks lastPts_ = INT64_MIN;

  std::mutex wmu_;
  std::condition_variable wcv_;
  std::deque<std::shared_ptr<Segment>> spillQueue_;
  bool writerBusy_ = false;
  bool stop_ = false;
  std::atomic<bool> spillEnabled_{false};
  uint64_t fileCounter_ = 0;
  std::thread writer_;
};

}  // namespace clipper
