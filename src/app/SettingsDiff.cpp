#include "app/SettingsDiff.h"
#include <tuple>

namespace clipper {

static auto captureFields(const Settings& s) {
  return std::tie(s.monitorIndex, s.outputHeight, s.fps, s.codec, s.quality, s.customKbps, s.captureCursor, s.replaySeconds,
                  s.ramThresholdSeconds, s.tempDir, s.audioBlacklist, s.micDeviceId, s.captureMic, s.mixTrack, s.audioKbps);
}

bool captureChanged(const Settings& a, const Settings& b) { return captureFields(a) != captureFields(b); }

}  // namespace clipper
