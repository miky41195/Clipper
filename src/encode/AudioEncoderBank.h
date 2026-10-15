#pragma once
#include "encode/AudioEncoder.h"
#include <map>
#include <memory>
#include <mutex>

namespace clipper {

// One AAC encoder per audio track, created/destroyed as the hub adds/removes tracks.
class AudioEncoderBank {
public:
  AudioEncoderBank(int kbps, AudioEncoder::PacketSink sink);
  TrackInfo onTrack(const TrackInfo& t);  // returns t with AAC extradata filled in
  void onChunk(int track, const int16_t* pcm, size_t frames, Ticks pts);
  void onTrackEnd(int track);  // flushes and destroys the encoder

private:
  int kbps_;
  AudioEncoder::PacketSink sink_;
  std::mutex mu_;
  std::map<int, std::unique_ptr<AudioEncoder>> enc_;
};

}  // namespace clipper
