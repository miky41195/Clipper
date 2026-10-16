#pragma once
#include "encode/AudioEncoder.h"
#include <map>
#include <memory>
#include <mutex>

namespace clipper {

// One AAC encoder per audio track, created/destroyed as the hub adds/removes tracks.
// When a track comes back (same id), the time it was away is filled with silent AAC frames so the
// track stays continuous in clips and recordings.
class AudioEncoderBank {
public:
  AudioEncoderBank(int kbps, AudioEncoder::PacketSink sink);
  TrackInfo onTrack(const TrackInfo& t);  // returns t with AAC extradata filled in
  void onChunk(int track, const int16_t* pcm, size_t frames, Ticks pts);
  void onTrackEnd(int track);  // flushes and destroys the encoder

private:
  void emit(Packet p);
  Bytes silentFrame();

  int kbps_;
  AudioEncoder::PacketSink sink_;
  std::recursive_mutex mu_;
  std::map<int, std::unique_ptr<AudioEncoder>> enc_;
  std::map<int, Ticks> lastEnd_;      // end of the last packet emitted per track
  std::map<int, Ticks> pendingFill_;  // revived track -> where its silence must start
  Bytes silent_;
};

}  // namespace clipper
