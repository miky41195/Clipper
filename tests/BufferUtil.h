#pragma once
#include "buffer/ReplayBuffer.h"
#include <cstring>
#include <vector>

// Payload encodes the pts so tests can verify byte-exact round trips.
inline clipper::Packet testPacket(int track, clipper::Ticks pts, bool key, size_t size = 100) {
  std::vector<uint8_t> data(size, 0);
  std::memcpy(data.data(), &pts, sizeof(pts));
  return clipper::Packet{track, pts, 0, key, std::make_shared<const std::vector<uint8_t>>(std::move(data))};
}

// Video at `fps`, keyframe every `fps` frames, from `start` for `secs` seconds. Returns the next pts.
inline clipper::Ticks feedVideo(clipper::ReplayBuffer& b, double secs, clipper::Ticks start = 0, int fps = 30) {
  const clipper::Ticks period = clipper::kTicksPerSecond / fps;
  const int frames = int(secs * fps);
  clipper::Ticks pts = start;
  for (int i = 0; i < frames; ++i, pts += period) b.push(testPacket(clipper::kVideoTrack, pts, (pts / period) % fps == 0));
  return pts;
}

inline clipper::TrackInfo videoTrack(int w = 1920, int h = 1080) {
  clipper::TrackInfo t;
  t.id = clipper::kVideoTrack;
  t.kind = clipper::TrackKind::Video;
  t.codec = clipper::Codec::H264;
  t.name = "Video";
  t.width = w;
  t.height = h;
  t.fps = 30;
  return t;
}

inline clipper::TrackInfo audioTrack(int id, const char* name) {
  clipper::TrackInfo t;
  t.id = id;
  t.kind = clipper::TrackKind::Audio;
  t.codec = clipper::Codec::AAC;
  t.name = name;
  return t;
}

inline std::vector<clipper::Packet> collect(const clipper::ReplayBuffer::Snapshot& s) {
  std::vector<clipper::Packet> out;
  s.forEach([&](const clipper::Packet& p) { out.push_back(p); });
  return out;
}
