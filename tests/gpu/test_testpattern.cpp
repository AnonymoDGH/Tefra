#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "runner.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"

#include <cstdlib>
#include <string>

using namespace tefra;

namespace {

bool update_golden() {
    const char* v = std::getenv("TEFRA_UPDATE_GOLDEN");
    return v && v[0] == '1';
}

app::RunConfig base_config() {
    app::RunConfig cfg;
    cfg.width = 640;
    cfg.height = 360;
    cfg.frames = 3;
    cfg.validation = true;
    cfg.strict_baseline = true;
    return cfg;
}

// Compares against tests/golden/<name>.png. Thresholds absorb rasterizer/driver rounding, not content changes.
void check_golden(const char* name, const ImageRGBA8& img) {
    const std::string path = std::string(TEFRA_GOLDEN_DIR) + "/" + name + ".png";
    const std::string actual = std::string(name) + ".actual.png";
    image_write_png(actual.c_str(), img);
    if (update_golden()) {
        REQUIRE(image_write_png(path.c_str(), img));
        MESSAGE("golden updated: " << path);
        return;
    }
    ImageRGBA8 golden;
    REQUIRE_MESSAGE(image_read_png(path.c_str(), golden),
                    "missing golden " << path << " (run with TEFRA_UPDATE_GOLDEN=1 and review the image)");
    const ImageDiff d = image_compare(img, golden, 8);
    INFO("rmse=" << d.rmse << " max_abs=" << d.max_abs << " over=" << d.fraction_over << " actual=" << actual);
    CHECK(d.size_match);
    CHECK(d.rmse < 1.5);
    CHECK(d.fraction_over < 0.002);
}

} // namespace

TEST_CASE("testpattern: renders headless, matches golden, zero validation errors") {
    app::RunConfig cfg = base_config();
    cfg.capture_to_memory = true;
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_enabled);
    CHECK(r.validation_errors == 0);
    CHECK(r.frames_rendered == 3);
    REQUIRE(r.capture.width == 640);
    REQUIRE(r.capture.height == 360);
    check_golden("testpattern", r.capture);
}

TEST_CASE("testpattern: sustained frames with animation stay valid and produce timings") {
    app::RunConfig cfg = base_config();
    cfg.frames = 12;
    cfg.warmup = 2;
    cfg.animate = true;
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_errors == 0);
    CHECK(r.stats.cpu_ms.size() == 10);
    CHECK(r.stats.frame_ms.size() >= 9);
    // Timestamps lag by frames in flight; lavapipe supports them.
    CHECK(!r.stats.gpu_ms.empty());
    CHECK(r.stats.pass_ms.count("testpattern") == 1);
    CHECK(app::write_bench_json("gpu_test_bench.json", cfg, r));
}

TEST_CASE("strict baseline clamps limits to the reference hardware") {
    rhi::DeviceDesc dd;
    dd.strict_baseline = true;
    auto dev = rhi::Device::create(dd);
    REQUIRE(dev != nullptr);
    CHECK(dev->caps().max_push_constants <= 128);
    CHECK(dev->caps().max_compute_shared_memory <= 32768);
    CHECK_FALSE(dev->caps().shader_draw_parameters);
    CHECK(dev->adapter().meets_baseline);
}

TEST_CASE("materials: deferred PBR + IBL + cascades, matches golden, zero validation errors") {
    app::RunConfig cfg = base_config();
    cfg.scene = "materials";
    cfg.no_bloom = true; // the golden covers the PBR path; camera post has its own test below
    cfg.no_film = true;
    cfg.capture_to_memory = true;
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_errors == 0);
    CHECK(r.draw_calls > 36);
    REQUIRE(r.capture.width == 640);
    check_golden("materials", r.capture);
}

TEST_CASE("valley: terrain + atmosphere + GPU-driven vegetation + water render with zero validation errors") {
    app::RunConfig cfg = base_config();
    cfg.scene = "valley";
    cfg.width = 320;
    cfg.height = 180;
    cfg.frames = 3; // > frames in flight: exercises cross-frame hazards on cached transients
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_errors == 0);
    CHECK(r.frames_rendered == 3);
}

TEST_CASE("valley: bloom, auto exposure and film effects run with zero validation errors") {
    app::RunConfig cfg = base_config();
    cfg.scene = "valley";
    cfg.width = 320;
    cfg.height = 180;
    cfg.frames = 3; // > frames in flight: the exposure state is read back by the slot that wrote it
    cfg.auto_exposure = true;
    const app::RunResult r = app::run(cfg);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.validation_errors == 0);
    CHECK(r.frames_rendered == 3);
}
