#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "runner.h"
#include "scenes.h"

#include "tefra/core/math.h"
#include "tefra/renderer/atmosphere.h"
#include "tefra/renderer/environment.h"
#include "tefra/renderer/scene.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/sky.h"

using namespace tefra;

namespace {

// Midnight at latitude 20: the sun is below the horizon, the full moon is up towards +Z (yaw 180 looks at it).
void build_night(render::Scene& scene, render::Environment& env, render::Atmosphere& atmo, app::SceneSetup& setup) {
    world::DataModel dm;
    world::Instance* light = dm.service("Lighting");
    light->set("ClockTime", world::Value::number(0.0));
    world::Instance* sky = dm.create("Sky", light, "Sky");
    sky->set("StarCount", world::Value::number(6000.0));
    world::sky_settings(dm, setup.settings);
    setup.use_atmosphere = true; // the procedural sky (stars and moon are drawn in its sky_color)
    env.build_from_equirect(atmo.render_equirect(setup.settings.sun_direction, 128));
    setup.camera.position = {0.0, 1.7, 0.0};
    setup.camera.yaw = radians(180.0f);
    setup.camera.pitch = radians(70.0f);
    (void)scene;
}

// Mean luminance (0..255) and the number of bright pixels (the moon disc, stars).
void count_pixels(const ImageRGBA8& img, double& mean, u32& bright) {
    double sum = 0.0;
    bright = 0;
    for (usize i = 0; i + 3 < img.pixels.size(); i += 4) {
        const u32 l = (img.pixels[i] * 54u + img.pixels[i + 1] * 183u + img.pixels[i + 2] * 19u) >> 8;
        sum += l;
        if (l > 200) ++bright;
    }
    mean = sum / double(img.width * img.height);
}

} // namespace

TEST_CASE("sky: a night frame with the moon and stars renders with zero validation errors and is not black") {
    app::RunConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.frames = 3;
    cfg.validation = true;
    cfg.strict_baseline = true;
    cfg.capture_to_memory = true;
    cfg.scene = "custom";
    cfg.build_custom = build_night;
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_errors == 0);
    REQUIRE(r.capture.width == 320);
    double mean = 0.0;
    u32 bright = 0;
    count_pixels(r.capture, mean, bright);
    INFO("mean luminance " << mean << ", bright pixels " << bright);
    CHECK(mean > 0.5);  // a dark, not black, night
    CHECK(bright > 10); // the moon disc (about 10 px across in this view)
}
