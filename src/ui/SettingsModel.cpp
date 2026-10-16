#include "ui/SettingsModel.h"
#include "capture/Monitors.h"
#include "core/SizeEstimate.h"
#include "ui/I18n.h"
#include <format>

namespace clipper {

bool SettingsForm::dirty() const { return settingsToJson(draft) != settingsToJson(saved); }

std::string durationLabel(int s) {
  if (s < 60) return std::format("{} s", s);
  if (s % 60 == 0) return std::format("{} min", s / 60);
  return std::format("{} min {} s", s / 60, s % 60);
}

int videoKbpsFor(const Settings& s, int captureW, int captureH) {
  const auto [w, h] = computeOutputSize(captureW, captureH, s.outputHeight);
  return effectiveVideoKbps(s, w, h);
}

std::string estimateLine(const Settings& s, int w, int h, int tracks) {
  const auto bytes = estimateBytes({videoKbpsFor(s, w, h), s.audioKbps, tracks, double(s.replaySeconds)});
  return trf("est.clip", formatBytes(bytes), durationLabel(s.replaySeconds));
}

std::string recordingLine(const Settings& s, int w, int h, int tracks) {
  const auto bytes = estimateBytes({videoKbpsFor(s, w, h), s.audioKbps, tracks, 60.0});
  return trf("est.rec", formatBytes(bytes));
}

std::string bufferLine(const Settings& s, int w, int h, int tracks) {
  const auto b = estimateBuffer({videoKbpsFor(s, w, h), s.audioKbps, tracks, double(s.replaySeconds)}, s.ramThresholdSeconds);
  if (b.diskBytes == 0) return trf("est.ram", formatBytes(b.ramBytes));
  return trf("est.disk", formatBytes(b.diskBytes), formatBytes(b.ramBytes));
}

}  // namespace clipper
