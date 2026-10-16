#pragma once
#include "output/ClipWriter.h"
#include "session/PacketBus.h"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace clipper {

// Manual recording: muxes live packets from the bus into "<stem>.rec.mkv" (crash tolerant),
// then remuxes to MP4 on stop() when requested. Tracks are fixed when recording starts.
// Disk writes happen on the recorder's own thread, so a slow disk never stalls the encoders.
class Recorder {
public:
  Recorder(PacketBus& bus, std::vector<TrackInfo> tracks, std::filesystem::path finalPath, std::string container,
           std::function<void()> requestKeyframe, const std::string& app = {});
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  // Stops receiving packets; after this the bus may be destroyed. Call on the thread that owns the bus.
  void detach();
  std::filesystem::path stop();  // idempotent; detaches, writes what is queued, finalizes, returns the final file
  Ticks duration() const;
  void setWriteHookForTest(std::function<void()> hook);

private:
  void writerLoop();

  PacketBus& bus_;
  std::filesystem::path finalPath_;
  std::filesystem::path workPath_;
  std::string container_;
  std::unique_ptr<ClipWriter> writer_;  // used only by the writer thread until stop()
  int subscription_ = 0;
  bool detached_ = false;
  bool stopped_ = false;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Packet> queue_;
  bool draining_ = false;
  Ticks first_ = -1;
  Ticks last_ = -1;
  std::function<void()> writeHook_;
  std::exception_ptr writeError_;
  std::thread thread_;
};

}  // namespace clipper
