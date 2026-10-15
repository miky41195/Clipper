#pragma once
#include "audio/PcmSource.h"
#include "core/Media.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace clipper {

struct AudioHubConfig {
  int sampleRate = 48000;
  int channels = 2;
  size_t chunkFrames = 480;  // 10 ms
  bool mixTrack = true;
};

// Owns the audio tracks. Every tick emits each whole 10 ms chunk (ending <= now) for every track,
// built from the time-aligned PCM of the sources attached to it; silence where there is no audio.
class AudioHub {
public:
  using ChunkSink = std::function<void(int track, const int16_t* pcm, size_t frames, Ticks pts)>;
  using TrackSink = std::function<void(const TrackInfo&)>;
  using EndSink = std::function<void(int track)>;

  AudioHub(AudioHubConfig cfg, ChunkSink onChunk, TrackSink onTrack, EndSink onEnd);

  void start(Ticks origin);
  int addTrack(const std::string& name);
  void removeTrack(int id);
  void attach(int id, std::shared_ptr<PcmSource> src);
  void detach(int id, const std::shared_ptr<PcmSource>& src);
  int trackForNewApp(const std::string& name);  // while pinned: the shared "Altre app" track
  void pin();
  void unpin();
  bool pinned() const;
  void tick(Ticks now);
  std::vector<TrackInfo> tracks() const;
  int mixTrackId() const { return mixId_; }
  int othersTrackId() const;

private:
  struct Track {
    TrackInfo info;
    std::vector<std::shared_ptr<PcmSource>> sources;
  };
  int addTrackLocked(const std::string& name);

  AudioHubConfig cfg_;
  ChunkSink onChunk_;
  TrackSink onTrack_;
  EndSink onEnd_;
  mutable std::mutex mu_;
  std::map<int, Track> tracks_;
  int nextId_ = 1;
  int mixId_ = -1;
  int othersId_ = -1;
  bool pinned_ = false;
  bool started_ = false;
  Ticks origin_ = 0;
  uint64_t chunkIndex_ = 0;
  std::vector<int32_t> acc_, mix_;
  std::vector<int16_t> buf_, tmp_;
};

}  // namespace clipper
