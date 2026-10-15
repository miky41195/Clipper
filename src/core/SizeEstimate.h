#pragma once
#include "core/Media.h"
#include <cstdint>
#include <string>

namespace clipper {

constexpr double kRamSegmentSeconds = 1.0;
constexpr double kDiskSegmentSeconds = 10.0;

struct SizeInputs {
  int videoKbps = 0;
  int audioKbps = 0;
  int audioTracks = 0;
  double seconds = 0;
};

struct BufferEstimate {
  uint64_t ramBytes = 0;
  uint64_t diskBytes = 0;
};

uint64_t estimateBytes(const SizeInputs& in);
BufferEstimate estimateBuffer(const SizeInputs& in, double ramThresholdSeconds);
int presetVideoKbps(Quality q, Codec c, int width, int height, int fps);
std::string formatBytes(uint64_t bytes);

}  // namespace clipper
