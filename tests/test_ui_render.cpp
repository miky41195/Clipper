#include <doctest/doctest.h>
#include <imgui.h>
#include "TestUtil.h"
#include "ui/ImageIO.h"
#include "ui/UiWindow.h"

using namespace clipper;

TEST_SUITE("gpu") {
  TEST_CASE("offscreen render writes a PNG of the themed page") {
    const auto dir = freshTempDir("ui_render");
    const auto png = dir / "page.png";
    const bool ok = renderOffscreen(640, 400, 1.0f, 0xFF84DC3D, [](const UiFonts&) {
      ImGui::TextUnformatted("Ciao");
    }, png);
    REQUIRE(ok);
    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    REQUIRE(readImage(png, bgra, w, h));
    CHECK(w == 640);
    CHECK(h == 400);
    const uint8_t* c = &bgra[(size_t(h / 2) * w + w / 2) * 4];
    // theme background #0F1115 (BGRA order)
    CHECK(int(c[0]) == 0x15);
    CHECK(int(c[1]) == 0x11);
    CHECK(int(c[2]) == 0x0F);
  }

  TEST_CASE("image round trip as jpeg") {
    const auto dir = freshTempDir("ui_jpeg");
    std::vector<uint8_t> px(32 * 16 * 4, 200);
    REQUIRE(writeImage(dir / "a.jpg", px.data(), 32, 16, 32 * 4, ImageFormat::Jpeg));
    std::vector<uint8_t> back;
    int w = 0, h = 0;
    REQUIRE(readImage(dir / "a.jpg", back, w, h));
    CHECK(w == 32);
    CHECK(h == 16);
  }
}
