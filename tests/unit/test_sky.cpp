#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/image.h"
#include "tefra/renderer/environment.h"
#include "tefra/renderer/renderer.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/sky.h"

#include <cmath>

using namespace tefra;
using namespace tefra::world;

namespace {
Instance* lighting(DataModel& dm) { return dm.service("Lighting"); }
} // namespace

// Without Sky, Atmosphere or Clouds, the Lighting props give the look the Studio had before those objects existed.
TEST_CASE("sky: without Sky, Atmosphere or Clouds the Lighting gives the built-in look") {
    DataModel dm;
    lighting(dm)->set("CloudCover", Value::number(0.45));
    render::RenderSettings s;
    sky_settings(dm, s);
    CHECK(s.cloud_coverage == doctest::Approx(0.45f));
    CHECK(s.cloud_density == doctest::Approx(1.0f));
    CHECK(s.cloud_wind == doctest::Approx(6.0f));
    CHECK(s.cloud_altitude == doctest::Approx(1600.0f));
    CHECK(s.cloud_size == doctest::Approx(2200.0f));
    CHECK(s.fog_density == doctest::Approx(0.0f));
    CHECK(s.fog_height == doctest::Approx(0.0f));
    CHECK(s.fog_tint.x == doctest::Approx(1.0f));
    CHECK(s.sky_glare == doctest::Approx(0.0f));
    CHECK(s.celestial_shown);
    CHECK(s.moon_visible);
    CHECK(s.star_count == doctest::Approx(3000.0f));
    // ClockTime 14 (default): day, sun up, exposure at the day value.
    CHECK(s.sun_direction.y > 0.5f);
    CHECK(s.ev100 == doctest::Approx(14.8f));
}

TEST_CASE("sky: Clouds map cover, density, colour, layer and wind") {
    DataModel dm;
    Instance* c = dm.create("Clouds", lighting(dm), "Clouds");
    REQUIRE(c != nullptr);
    c->set("Cover", Value::number(0.6));
    c->set("Density", Value::number(2.0));
    c->set("Color", Value::color3({0.9f, 0.8f, 0.7f}));
    c->set("Altitude", Value::number(900.0));
    c->set("Size", Value::number(3000.0));
    c->set("WindSpeed", Value::number(10.0));
    c->set("WindDirection", Value::number(90.0));
    render::RenderSettings s;
    sky_settings(dm, s);
    CHECK(s.cloud_coverage == doctest::Approx(0.6f));
    CHECK(s.cloud_density == doctest::Approx(2.0f));
    CHECK(s.cloud_tint.x == doctest::Approx(0.9f));
    CHECK(s.cloud_tint.z == doctest::Approx(0.7f));
    CHECK(s.cloud_altitude == doctest::Approx(900.0f));
    CHECK(s.cloud_size == doctest::Approx(3000.0f));
    CHECK(s.cloud_wind == doctest::Approx(10.0f));
    CHECK(s.cloud_wind_dir.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(s.cloud_wind_dir.y == doctest::Approx(1.0f));
    // The Clouds object replaces CloudCover; Enabled = false turns the layer off.
    c->set("Enabled", Value::boolean(false));
    sky_settings(dm, s);
    CHECK(s.cloud_coverage == doctest::Approx(0.0f));
}

TEST_CASE("sky: Atmosphere maps fog, tint, glare and haze") {
    DataModel dm;
    Instance* a = dm.create("Atmosphere", lighting(dm), "Atmosphere");
    REQUIRE(a != nullptr);
    a->set("Density", Value::number(0.5));
    a->set("Offset", Value::number(120.0));
    a->set("Color", Value::color3({1.0f, 0.5f, 0.25f}));
    a->set("Glare", Value::number(2.0));
    a->set("Decay", Value::color3({0.5f, 1.0f, 1.0f}));
    a->set("Haze", Value::number(3.0));
    render::RenderSettings s;
    sky_settings(dm, s);
    CHECK(s.fog_density == doctest::Approx(0.005f));
    CHECK(s.fog_height == doctest::Approx(120.0f));
    CHECK(s.fog_tint.y == doctest::Approx(0.5f));
    CHECK(s.sky_glare == doctest::Approx(2.0f));
    render::AtmosphereParams p;
    atmosphere_params(dm, p);
    CHECK(p.haze == doctest::Approx(3.0f));
    const render::AtmosphereParams def;
    CHECK(p.rayleigh_scattering.x == doctest::Approx(def.rayleigh_scattering.x * 0.5f));
    CHECK(p.rayleigh_scattering.y == doctest::Approx(def.rayleigh_scattering.y));
    CHECK_FALSE(same_atmosphere(p, def));
    CHECK(same_atmosphere(p, p));
}

// Night: the sun is below the horizon, the full moon is up (opposite the sun) and gives the light; stars are on.
TEST_CASE("sky: at night the full moon is up and lights the scene; a new moon gives no light") {
    DataModel dm;
    lighting(dm)->set("ClockTime", Value::number(0.0));
    Instance* sky = dm.create("Sky", lighting(dm), "Sky");
    REQUIRE(sky != nullptr);
    sky->set("StarCount", Value::number(500.0));
    sky->set("StarBrightness", Value::number(0.5));
    render::RenderSettings s;
    sky_settings(dm, s);
    CHECK(s.sun_direction.y < -0.5f);
    CHECK(s.moon_direction.y > 0.5f);
    CHECK(dot(s.moon_direction, s.sun_direction) == doctest::Approx(-1.0f).epsilon(1e-3));
    CHECK(s.moon_illuminance == doctest::Approx(40.0f));
    CHECK(s.star_count == doctest::Approx(500.0f));
    CHECK(s.star_brightness == doctest::Approx(0.5f));
    CHECK(s.ev100 < 9.0f); // dark exposure at night

    sky->set("MoonPhase", Value::number(0.0));
    sky_settings(dm, s);
    CHECK(s.moon_illuminance == doctest::Approx(0.0f));
    CHECK(dot(s.moon_direction, s.sun_direction) == doctest::Approx(1.0f).epsilon(1e-3));

    // Half moon: a quarter of the sky elongation, no light change to the sun.
    sky->set("MoonPhase", Value::number(0.25));
    sky_settings(dm, s);
    CHECK(dot(s.moon_direction, s.sun_direction) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(s.moon_illuminance == doctest::Approx(20.0f));

    // Moon hidden: no disc and no light.
    sky->set("MoonVisible", Value::boolean(false));
    sky->set("MoonPhase", Value::number(0.5));
    sky_settings(dm, s);
    CHECK(s.moon_illuminance == doctest::Approx(0.0f));
    CHECK_FALSE(s.moon_visible);
}

TEST_CASE("sky: moon direction is the sun turned by the elongation") {
    const vec3 sun = normalize(vec3{0.3f, 0.6f, -0.7f});
    CHECK(length(moon_direction(sun, 0.0f) - sun) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(length(moon_direction(sun, 0.5f) + sun) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(length(moon_direction(sun, 0.7f)) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("sky: background description and its equality") {
    DataModel dm;
    CHECK(sky_background(dm).mode == SkyboxMode::Procedural);
    Instance* sky = dm.create("Sky", lighting(dm), "Sky");
    REQUIRE(sky != nullptr);
    sky->set("SkyboxMode", Value::enumeration(1)); // Cubemap
    sky->set("SkyboxRt", Value::string("right.png"));
    sky->set("SkyboxFt", Value::string("front.png"));
    const SkyBackground a = sky_background(dm);
    CHECK(a.mode == SkyboxMode::Cubemap);
    CHECK(a.faces[3] == "right.png");
    CHECK(a == sky_background(dm));
    sky->set("SkyboxFt", Value::string("other.png"));
    CHECK(a != sky_background(dm));
    sky->set("SkyboxMode", Value::enumeration(2)); // HDRI
    sky->set("HdriPath", Value::string("sky.hdr"));
    const SkyBackground h = sky_background(dm);
    CHECK(h.mode == SkyboxMode::Hdri);
    CHECK(h.hdri == "sky.hdr");
    CHECK(h != a);
    // The default Sky size is the sun of the built-in atmosphere.
    render::AtmosphereParams p;
    atmosphere_params(dm, p);
    CHECK(p.sun_angular_radius == doctest::Approx(0.004675f).epsilon(1e-3));
}

// Six faces resampled: a constant face gives its value; the Roblox orientation puts Rt at +X.
TEST_CASE("sky: cubemap faces become an equirect with the Roblox orientation") {
    ImageRGBA8 faces[6];
    for (ImageRGBA8& f : faces) {
        f.width = f.height = 8;
        f.pixels.assign(8 * 8 * 4, 0);
        for (usize i = 3; i < f.pixels.size(); i += 4) f.pixels[i] = 255;
    }
    // Rt (+X) red, Up (+Y) white.
    for (usize i = 0; i < faces[3].pixels.size(); i += 4) faces[3].pixels[i] = 255;
    for (usize i = 0; i < faces[4].pixels.size(); i += 4) {
        faces[4].pixels[i] = faces[4].pixels[i + 1] = faces[4].pixels[i + 2] = 255;
    }
    const ImageRGBA32F eq = render::equirect_from_cube_faces(faces, 64, 1000.0f);
    REQUIRE(eq.width == 64);
    REQUIRE(eq.height == 32);
    auto px = [&](u32 x, u32 y, int c) { return eq.pixels[(usize(y) * eq.width + x) * 4 + usize(c)]; };
    // +X: u = 0.75, v = 0.5 (centre of the right face): red, no green.
    CHECK(px(48, 16, 0) > 900.0f);
    CHECK(px(48, 16, 1) < 1.0f);
    // -Z (front, u = 0.5): the black face.
    CHECK(px(32, 16, 0) < 1.0f);
    // +Y (top of the equirect, v near 0): white.
    CHECK(px(10, 1, 0) > 900.0f);
    CHECK(px(10, 1, 2) > 900.0f);
    // A missing face gives an empty image.
    faces[0].width = 0;
    CHECK(render::equirect_from_cube_faces(faces, 64, 1000.0f).width == 0);
}
