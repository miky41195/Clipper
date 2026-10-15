#pragma once
#include "output/ClipWriter.h"
#include "session/PacketBus.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

namespace clipper {

// Manual recording: muxes live packets from the bus into "<stem>.rec.mkv" (crash tolerant),
// then remuxes to MP4 on stop() when requested. Tracks are fixed when recording starts.
class Recorder {
public:
  Recorder(PacketBus& bus, std::vector<TrackInfo> tracks, std::filesystem::path finalPath, std::string container,
           std::function<void()> requestKeyframe);
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  std::filesystem::path stop();  // idempotent; returns the final file
  Ticks duration() const;

private:
  PacketBus& bus_;
  std::filesystem::path finalPath_;
  std::filesystem::path workPath_;
  std::string container_;
  mutable std::mutex mu_;
  std::unique_ptr<ClipWriter> writer_;
  int subscription_ = 0;
  bool stopped_ = false;
  Ticks first_ = -1;
  Ticks last_ = -1;
};

}  // namespace clipper
