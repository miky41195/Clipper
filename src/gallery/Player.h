#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace clipper {

double clampSeek(double seconds, double duration);
// out[i] = clamp(sum_k gains[k] * tracks[k][i], -1, 1)
void mixTracks(const std::vector<const float*>& tracks, const std::vector<float>& gains, float* out, size_t samples);

// Clip preview: video (hardware decode when available) scaled to <= 1280 px, every audio track decoded
// separately and mixed live with per-track gain/mute. The audio clock drives playback.
class Player {
public:
  explicit Player(const std::filesystem::path& file);
  ~Player();
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  bool ok() const;
  const std::string& error() const;
  double duration() const;
  std::vector<std::string> trackNames() const;
  void setTrackGain(int index, float gain);  // 0..2
  void setTrackEnabled(int index, bool enabled);
  float trackGain(int index) const;
  bool trackEnabled(int index) const;
  float trackLevel(int index) const;  // recent peak 0..1, for meters

  void play();
  void pause();
  bool playing() const;
  void seek(double seconds);
  double position() const;
  // Frame due now, as BGRA. Returns true when it differs from the previously returned one.
  bool takeFrame(std::vector<uint8_t>& bgra, int& width, int& height);

  struct Impl;

private:
  std::unique_ptr<Impl> d_;
};

}  // namespace clipper
