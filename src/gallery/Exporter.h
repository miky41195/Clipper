#pragma once
#include <filesystem>
#include <functional>
#include <vector>

namespace clipper {

struct ExportOptions {
  std::vector<bool> keep;    // per audio track of the source, in order
  std::vector<float> gain;   // per audio track, 0..2
  bool alsoSeparate = false; // also copy the kept tracks unchanged after the Mix
};

// Writes "<stem>_export.<ext>" next to the source: video stream-copied, kept tracks mixed into one
// AAC "Mix" track. Throws std::runtime_error on failure. progress receives 0..1.
std::filesystem::path exportClip(const std::filesystem::path& src, const ExportOptions& opt,
                                 const std::function<void(float)>& progress);

}  // namespace clipper
