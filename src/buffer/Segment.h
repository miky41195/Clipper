#pragma once
#include "core/Media.h"
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <vector>

namespace clipper {

// A run of packets starting at a video keyframe. Lives in RAM until spill() moves it to a file.
class Segment {
public:
  explicit Segment(Ticks start);
  ~Segment();
  Segment(const Segment&) = delete;
  Segment& operator=(const Segment&) = delete;

  void add(Packet p);
  bool spill(const std::filesystem::path& file);  // false on I/O error; RAM copy is kept then
  void forEach(const std::function<void(const Packet&)>& fn) const;

  Ticks start() const { return start_; }
  size_t ramBytes() const;
  bool hasTrack(int id) const;
  std::set<int> trackIds() const;

private:
  mutable std::mutex mu_;
  const Ticks start_;
  std::vector<Packet> packets_;
  std::set<int> tracks_;
  size_t ramBytes_ = 0;
  std::filesystem::path file_;
};

}  // namespace clipper
