#pragma once
#include "buffer/ReplayBuffer.h"
#include "core/Media.h"
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace clipper {

// Muxes already-encoded packets into MP4/MKV without re-encoding. Writes "<final>.tmp" and renames on finish().
class ClipWriter {
public:
  ClipWriter(std::filesystem::path finalPath, std::string_view container, std::vector<TrackInfo> tracks);
  ~ClipWriter();
  ClipWriter(const ClipWriter&) = delete;
  ClipWriter& operator=(const ClipWriter&) = delete;

  // Output starts at the first video keyframe; earlier audio with pts >= that keyframe is kept.
  void write(const Packet& p);
  void finish();
  Ticks origin() const;

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

void writeSnapshot(const ReplayBuffer::Snapshot& snap, const std::filesystem::path& file, std::string_view container);
void remux(const std::filesystem::path& src, const std::filesystem::path& dst);
std::vector<uint8_t> extractExtradata(Codec codec, const std::vector<uint8_t>& keyframe);

}  // namespace clipper
