#include <doctest/doctest.h>
#include "MediaGen.h"
#include "TestUtil.h"
#include "encode/AudioEncoder.h"
#include "output/ClipWriter.h"
#include "ui/AppUi.h"
#include "ui/UiPreview.h"

using namespace clipper;
namespace fs = std::filesystem;

namespace {
void makeClip(const fs::path& file, const char* app, uint8_t luma) {
  static const GpuDevice g = createGpuDevice();
  TrackInfo v;
  auto packets = encodeTestVideo(g, {Codec::H264, 640, 360, 30, 1500, false}, 1.5, 0, [=](int i) { return uint8_t(luma + i % 20); }, &v);
  std::vector<TrackInfo> tracks{v};
  int id = 1;
  for (const char* name : {"Mix", "Microfono", "Discord"}) {
    TrackInfo a;
    a.id = id;
    a.kind = TrackKind::Audio;
    a.name = name;
    AudioEncoder enc({48000, 2, 96}, id, [&](Packet p) { packets.push_back(std::move(p)); });
    std::vector<int16_t> pcm(960, 0);
    for (int c = 0; c < 150; ++c) enc.encode(pcm.data(), 480, Ticks(c) * 100'000);
    enc.flush();
    a.extradata = enc.extradata();
    tracks.push_back(a);
    ++id;
  }
  std::stable_sort(packets.begin(), packets.end(), [](const Packet& x, const Packet& y) { return x.pts < y.pts; });
  ClipWriter w(file, "mp4", tracks);
  w.setApp(app);
  for (const auto& p : packets) w.write(p);
  w.finish();
}
}  // namespace

TEST_SUITE("gpu") {
  TEST_CASE("gallery and player pages render real clips") {
    const auto dir = freshTempDir("gallery_page");
    makeClip(dir / "Valorant_ace.mp4", "Valorant", 60);
    makeClip(dir / "Bazaar_win.mp4", "The Bazaar", 120);
    makeClip(dir / "Clutch 1v3.mp4", "Valorant", 180);
    UiContext ctx = previewContext();
    ctx.clipsFolder = [dir](const Settings&) { return dir; };
    ctx.thumbnailCache = [dir] { return dir / "thumbs"; };
    CHECK(renderPreview("gallery", dir / "gallery.png", 1.0f, &ctx, [](AppUi& a) {
      a.gallery().prepareForSnapshot();
      CHECK(a.gallery().clips().size() == 3);
    }));
    CHECK(renderPreview("gallery", dir / "player.png", 1.0f, &ctx, [dir](AppUi& a) {
      a.gallery().refresh();
      a.gallery().openClip(dir / "Bazaar_win.mp4");
      a.gallery().prepareForSnapshot();
    }));
    CHECK(fs::file_size(dir / "gallery.png") > 20'000);
    CHECK(fs::file_size(dir / "player.png") > 20'000);
  }
}
