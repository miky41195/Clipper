#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clipper {

struct ClipInfo {
  std::filesystem::path path;
  std::string name;  // file stem, UTF-8
  std::string app;   // "—" when the clip was not made by Clipper
  int64_t bytes = 0;
  double seconds = 0;
  int width = 0;
  int height = 0;
  std::vector<std::string> tracks;  // audio track names
  std::chrono::system_clock::time_point modified;
};

std::optional<ClipInfo> probeClip(const std::filesystem::path& file);

// Clips in one folder, newest first. Probe results are cached in a JSON file keyed by path+size+mtime.
class ClipLibrary {
public:
  ClipLibrary(std::filesystem::path dir, std::filesystem::path cacheFile);
  const std::vector<ClipInfo>& refresh();
  const std::vector<ClipInfo>& clips() const { return clips_; }
  int probeCount() const { return probes_; }
  void setDir(std::filesystem::path dir) { dir_ = std::move(dir); }
  const std::filesystem::path& dir() const { return dir_; }

private:
  void loadCache();
  void saveCache() const;

  std::filesystem::path dir_;
  std::filesystem::path cacheFile_;
  struct Cached {
    int64_t size = 0;
    int64_t mtime = 0;
    ClipInfo info;
  };
  std::vector<std::pair<std::wstring, Cached>> cache_;
  std::vector<ClipInfo> clips_;
  int probes_ = 0;
};

std::vector<std::pair<std::string, std::vector<const ClipInfo*>>> groupByDay(const std::vector<ClipInfo>& clips,
                                                                             std::chrono::system_clock::time_point now);
bool matches(const ClipInfo& clip, std::string_view query);

}  // namespace clipper
