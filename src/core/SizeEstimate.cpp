#include "core/SizeEstimate.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace clipper {

uint64_t estimateBytes(const SizeInputs& in) {
  const double kbps = in.videoKbps + double(in.audioKbps) * in.audioTracks;
  return static_cast<uint64_t>(kbps * 1000.0 / 8.0 * in.seconds + 0.5);
}

BufferEstimate estimateBuffer(const SizeInputs& in, double ramThresholdSeconds) {
  if (in.seconds <= ramThresholdSeconds) {
    SizeInputs ram = in;
    ram.seconds += kRamSegmentSeconds;
    return {estimateBytes(ram), 0};
  }
  SizeInputs seg = in;
  seg.seconds = kDiskSegmentSeconds;
  SizeInputs disk = in;
  disk.seconds += kDiskSegmentSeconds;
  return {estimateBytes(seg), estimateBytes(disk)};
}

int presetVideoKbps(Quality q, Codec c, int width, int height, int fps) {
  const double base = q == Quality::Low ? 8000 : q == Quality::Medium ? 15000 : 25000;
  const double scale = double(width) * height * fps / (1920.0 * 1080.0 * 60.0);
  const double codecFactor = (c == Codec::AV1 || c == Codec::HEVC) ? 0.7 : 1.0;
  const int kbps = static_cast<int>(std::lround(base * scale * codecFactor / 100.0) * 100);
  return std::max(kbps, 1000);
}

std::string formatBytes(uint64_t b) {
  if (b < 1'000'000) return std::format("{} KB", (b + 500) / 1000);
  if (b < 1'000'000'000) return std::format("{} MB", (b + 500'000) / 1'000'000);
  return std::format("{:.1f} GB", double(b) / 1e9);
}

}  // namespace clipper
