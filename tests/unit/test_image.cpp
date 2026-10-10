#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/image.h"

using namespace tefra;

static ImageRGBA8 make(u32 w, u32 h, u8 v) {
    ImageRGBA8 img;
    img.width = w;
    img.height = h;
    img.pixels.assign(usize(w) * h * 4, v);
    return img;
}

TEST_CASE("image compare metrics") {
    ImageRGBA8 a = make(4, 4, 100), b = make(4, 4, 100);
    ImageDiff d = image_compare(a, b, 2);
    CHECK(d.size_match);
    CHECK(d.rmse == 0.0);
    CHECK(d.fraction_over == 0.0);
    b.pixels[0] = 110; // one channel of one pixel
    d = image_compare(a, b, 2);
    CHECK(d.max_abs == 10);
    CHECK(d.fraction_over == doctest::Approx(1.0 / 16.0));
    b.pixels[3] = 0; // alpha ignored
    CHECK(image_compare(a, b, 2).max_abs == 10);
    CHECK_FALSE(image_compare(a, make(2, 2, 0), 2).size_match);
}

TEST_CASE("PNG round trip") {
    ImageRGBA8 a = make(8, 3, 0);
    for (usize i = 0; i < a.pixels.size(); ++i) a.pixels[i] = static_cast<u8>(i * 7);
    REQUIRE(image_write_png("tefra_test_roundtrip.png", a));
    ImageRGBA8 b;
    REQUIRE(image_read_png("tefra_test_roundtrip.png", b));
    CHECK(b.width == 8);
    CHECK(b.height == 3);
    CHECK(b.pixels == a.pixels);
}
