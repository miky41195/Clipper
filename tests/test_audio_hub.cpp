#include <doctest/doctest.h>
#include <map>
#include <vector>
#include "audio/AudioHub.h"

using namespace clipper;

namespace {
constexpr Ticks kT0 = 1'000'000'000;
constexpr Ticks kMs = 10'000;

struct Recorder {
  struct Chunk {
    int track;
    Ticks pts;
    std::vector<int16_t> pcm;
  };
  std::vector<Chunk> chunks;
  std::vector<TrackInfo> added;
  std::vector<int> ended;

  AudioHub make(bool mix = true) {
    AudioHubConfig c;
    c.mixTrack = mix;
    return AudioHub(
        c,
        [this](int t, const int16_t* p, size_t f, Ticks pts) { chunks.push_back({t, pts, std::vector<int16_t>(p, p + f * 2)}); },
        [this](const TrackInfo& t) { added.push_back(t); }, [this](int id) { ended.push_back(id); });
  }
  std::vector<Chunk> of(int track) const {
    std::vector<Chunk> r;
    for (auto& c : chunks)
      if (c.track == track) r.push_back(c);
    return r;
  }
};

std::shared_ptr<PcmSource> sourceWith(int16_t v, size_t frames, Ticks ts) {
  auto s = std::make_shared<PcmSource>();
  std::vector<int16_t> d(frames * 2, v);
  s->push(d.data(), frames, ts);
  return s;
}
}  // namespace

TEST_CASE("mix track is created first and named Mix") {
  Recorder r;
  auto hub = r.make();
  REQUIRE(r.added.size() == 1);
  CHECK(r.added[0].name == "Mix");
  CHECK(hub.mixTrackId() == r.added[0].id);
  CHECK(r.added[0].id >= 1);
}

TEST_CASE("no mix track when disabled") {
  Recorder r;
  auto hub = r.make(false);
  CHECK(r.added.empty());
  CHECK(hub.mixTrackId() == -1);
}

TEST_CASE("chunks are exact 10 ms steps carrying source data") {
  Recorder r;
  auto hub = r.make(false);
  const int t = hub.addTrack("Gioco");
  hub.attach(t, sourceWith(1000, 960, kT0));
  hub.start(kT0);
  hub.tick(kT0 + 20 * kMs);
  const auto c = r.of(t);
  REQUIRE(c.size() == 2);
  CHECK(c[0].pts == kT0);
  CHECK(c[1].pts == kT0 + 10 * kMs);
  CHECK(c[0].pcm.front() == 1000);
  CHECK(c[1].pcm.back() == 1000);
}

TEST_CASE("track without data emits silence") {
  Recorder r;
  auto hub = r.make(false);
  const int t = hub.addTrack("Muto");
  hub.start(kT0);
  hub.tick(kT0 + 30 * kMs);
  const auto c = r.of(t);
  REQUIRE(c.size() == 3);
  CHECK(c[2].pcm[100] == 0);
}

TEST_CASE("sources on a track are summed and clamped") {
  Recorder r;
  auto hub = r.make(false);
  const int t = hub.addTrack("Chrome");
  hub.attach(t, sourceWith(1000, 480, kT0));
  hub.attach(t, sourceWith(2000, 480, kT0));
  const int u = hub.addTrack("Forte");
  hub.attach(u, sourceWith(30000, 480, kT0));
  hub.attach(u, sourceWith(30000, 480, kT0));
  hub.start(kT0);
  hub.tick(kT0 + 10 * kMs);
  CHECK(r.of(t)[0].pcm[0] == 3000);
  CHECK(r.of(u)[0].pcm[0] == 32767);
}

TEST_CASE("mix is the sum of all other tracks") {
  Recorder r;
  auto hub = r.make();
  const int a = hub.addTrack("A");
  const int b = hub.addTrack("B");
  hub.attach(a, sourceWith(100, 480, kT0));
  hub.attach(b, sourceWith(-30, 480, kT0));
  hub.start(kT0);
  hub.tick(kT0 + 10 * kMs);
  CHECK(r.of(hub.mixTrackId())[0].pcm[0] == 70);
}

TEST_CASE("removeTrack ends it and stops its chunks") {
  Recorder r;
  auto hub = r.make(false);
  const int t = hub.addTrack("X");
  hub.start(kT0);
  hub.tick(kT0 + 10 * kMs);
  hub.removeTrack(t);
  hub.tick(kT0 + 30 * kMs);
  CHECK(r.ended == std::vector<int>{t});
  CHECK(r.of(t).size() == 1);
}

TEST_CASE("while pinned new apps share one 'Altre app' track") {
  Recorder r;
  auto hub = r.make(false);
  const int before = hub.trackForNewApp("Discord");
  hub.pin();
  const int x = hub.trackForNewApp("Spotify");
  const int y = hub.trackForNewApp("Chrome");
  CHECK(x == y);
  CHECK(x != before);
  CHECK(r.added.back().name == "Altre app");
  hub.unpin();
  const int z = hub.trackForNewApp("Edge");
  CHECK(z != x);
  CHECK(r.added.back().name == "Edge");
}

TEST_CASE("tick before start does nothing; pts stays exact over many chunks") {
  Recorder r;
  auto hub = r.make(false);
  const int t = hub.addTrack("A");
  hub.tick(kT0 + 50 * kMs);
  CHECK(r.chunks.empty());
  hub.start(kT0);
  hub.tick(kT0 + 1000 * kMs);
  const auto c = r.of(t);
  REQUIRE(c.size() == 100);
  CHECK(c[99].pts == kT0 + 990 * kMs);
}
