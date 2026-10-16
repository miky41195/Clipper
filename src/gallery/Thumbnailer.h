#pragma once
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace clipper {

inline constexpr int kThumbW = 320;
inline constexpr int kThumbH = 180;

// JPEG thumbnail (frame near 1 s, letterboxed to 320x180) cached by path+size+mtime.
std::optional<std::filesystem::path> makeThumbnail(const std::filesystem::path& clip, const std::filesystem::path& cacheDir);

// One background thread producing thumbnails in request order.
class ThumbnailQueue {
public:
  struct Result {
    std::filesystem::path clip;
    std::optional<std::filesystem::path> thumbnail;
  };
  explicit ThumbnailQueue(std::filesystem::path cacheDir);
  ~ThumbnailQueue();
  void request(const std::filesystem::path& clip);
  std::vector<Result> poll();

private:
  void loop();
  std::filesystem::path cacheDir_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::filesystem::path> pending_;
  std::set<std::filesystem::path> queued_;
  std::vector<Result> done_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace clipper
