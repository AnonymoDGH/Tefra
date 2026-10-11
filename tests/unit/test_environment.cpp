#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/renderer/environment.h"
#include "tefra/renderer/mesh.h"

using namespace tefra;
using namespace tefra::render;

TEST_CASE("octahedral encoding round trip") {
    const vec3 dirs[] = {
        {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {0, 0, -1}, normalize(vec3{1, -2, 3}), normalize(vec3{-0.3f, 0.1f, -0.9f})};
    for (vec3 d : dirs) {
        const vec3 r = oct_decode(oct_encode(d));
        CHECK(dot(r, d) > 0.99999f);
    }
}

TEST_CASE("SH9 of a constant environment gives irradiance pi*L") {
    ImageRGBA32F img;
    img.width = 128;
    img.height = 64;
    img.pixels.assign(128 * 64 * 4, 1.0f);
    vec4 sh[9];
    sh9_project_equirect(img, sh);
    sh9_convolve_cosine(sh);
    for (vec3 n : {vec3{0, 1, 0}, vec3{1, 0, 0}, vec3{0, 0, -1}}) {
        const vec3 e = sh9_eval(sh, n);
        CHECK(e.x == doctest::Approx(kPi).epsilon(0.01));
    }
}

TEST_CASE("SH9 of a sky-only environment: up irradiance > down irradiance") {
    ImageRGBA32F img;
    img.width = 128;
    img.height = 64;
    img.pixels.assign(128 * 64 * 4, 0.0f);
    for (u32 y = 0; y < 32; ++y)
        for (u32 x = 0; x < 128; ++x)
            for (int c = 0; c < 3; ++c) img.pixels[(y * 128 + x) * 4 + c] = 1.0f;
    vec4 sh[9];
    sh9_project_equirect(img, sh);
    sh9_convolve_cosine(sh);
    CHECK(sh9_eval(sh, {0, 1, 0}).x == doctest::Approx(kPi).epsilon(0.05));
    CHECK(sh9_eval(sh, {0, -1, 0}).x < 0.2f);
}

TEST_CASE("BRDF LUT: energy bounded, smooth surfaces near 1 at normal incidence") {
    const vec2 smooth = brdf_lut_integrate(1.0f, 0.05f, 512);
    CHECK(smooth.x + smooth.y == doctest::Approx(1.0f).epsilon(0.03));
    const vec2 rough = brdf_lut_integrate(0.5f, 1.0f, 512);
    CHECK(rough.x + rough.y < 1.0f);
    CHECK(rough.x > 0.0f);
}

TEST_CASE("equirect direction convention: center is -Z, top is +Y") {
    const vec3 c = equirect_direction(0.5f, 0.5f);
    CHECK(c.z == doctest::Approx(-1.0f));
    CHECK(equirect_direction(0.3f, 0.0f).y == doctest::Approx(1.0f));
}
