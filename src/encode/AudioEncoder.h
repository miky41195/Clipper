#pragma once
#include "core/Media.h"
#include <functional>
#include <mftransform.h>
#include <vector>
#include <winrt/base.h>

namespace clipper {

struct AudioEncoderConfig {
  int sampleRate = 48000;
  int channels = 2;
  int kbps = 160;  // 96 / 128 / 160 / 192
};

// Microsoft AAC-LC encoder MFT (synchronous). Emits raw AAC frames (1024 samples each).
class AudioEncoder {
public:
  using PacketSink = std::function<void(Packet)>;
  AudioEncoder(const AudioEncoderConfig& cfg, int trackId, PacketSink sink);
  ~AudioEncoder();
  AudioEncoder(const AudioEncoder&) = delete;
  AudioEncoder& operator=(const AudioEncoder&) = delete;

  void encode(const int16_t* interleaved, size_t frames, Ticks pts);
  void flush();
  const std::vector<uint8_t>& extradata() const { return extradata_; }

  // Shift applied to output timestamps to compensate the encoder's priming delay (tuned by the A/V sync test).
  static constexpr Ticks kEncoderDelay = 0;

private:
  bool pullOne();

  AudioEncoderConfig cfg_;
  int track_;
  PacketSink sink_;
  winrt::com_ptr<IMFTransform> mft_;
  std::vector<uint8_t> extradata_;
};

}  // namespace clipper
