#include <doctest/doctest.h>
#include <thread>
#include "MediaGen.h"
#include "TestUtil.h"
#include "gallery/Thumbnailer.h"
#include "output/ClipWriter.h"
#include "ui/ImageIO.h"

using namespace clipper;
namespace fs = std::filesystem;

static void videoOnlyClip(const fs::path& file, Codec codec) {
  static const GpuDevice g = createGpuDevice();
  TrackInfo v;
  const auto packets =
      encodeTestVideo(g, {codec, 640, 360, 30, 2000, false}, 2.0, 0, [](int i) { return uint8_t(i < 30 ? 40 : 200); }, &v);
  ClipWriter w(file, "mp4", {v});
  for (const auto& p : packets) w.write(p);
  w.finish();
}

TEST_SUITE("gpu") {
  TEST_CASE("thumbnail is 320x180, taken around 1 s, and cached") {
    const auto dir = freshTempDir("thumbs");
    for (Codec c : {Codec::H264, Codec::AV1}) {
      const auto clip = dir / (c == Codec::AV1 ? "av1.mp4" : "h264.mp4");
      videoOnlyClip(clip, c);
      const auto thumb = makeThumbnail(clip, dir / "cache");
      REQUIRE(thumb.has_value());
      std::vector<uint8_t> px;
      int w = 0, h = 0;
      REQUIRE(readImage(*thumb, px, w, h));
      CHECK(w == 320);
      CHECK(h == 180);
      const uint8_t* centre = &px[(size_t(h / 2) * w + w / 2) * 4];
      CHECK(int(centre[1]) > 120);  // bright frame (after 1 s), not the dark start
      const auto before = fs::last_write_time(*thumb);
      CHECK(makeThumbnail(clip, dir / "cache") == thumb);
      CHECK(fs::last_write_time(*thumb) == before);
    }
  }

  TEST_CASE("thumbnail queue works in the background") {
    const auto dir = freshTempDir("thumbq");
    videoOnlyClip(dir / "a.mp4", Codec::H264);
    ThumbnailQueue q(dir / "cache");
    q.request(dir / "a.mp4");
    q.request(dir / "a.mp4");  // duplicate ignored
    std::vector<ThumbnailQueue::Result> ready;
    for (int i = 0; i < 100 && ready.empty(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ready = q.poll();
    }
    REQUIRE(ready.size() == 1);
    CHECK(ready[0].thumbnail.has_value());
  }

  TEST_CASE("thumbnail of a missing file fails cleanly") {
    const auto dir = freshTempDir("thumbmissing");
    CHECK_FALSE(makeThumbnail(dir / "nope.mp4", dir / "cache").has_value());
  }
}
