// tefra_bench: renders N frames and writes a tefra-bench/1 JSON report (spec §8.4).
// Performance numbers only count when produced on the reference PC (spec §6.6).
#include "options.h"

#include "tefra/core/log.h"

#include <cstdio>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_bench [options]\n"
                    "  --window                render to a window (default: headless)\n"
                    "  --out FILE.json         report path (default: stdout)\n");
        app::print_common_usage();
        return 0;
    }
    app::RunConfig cfg;
    cfg.frames = 600;
    cfg.warmup = 60;
    cfg.window = cl.has("window");
    cfg.present = cfg.window ? rhi::PresentMode::Immediate : rhi::PresentMode::Fifo;
    cfg.grass_stats = true; // the report always states how many grass blades were drawn
    if (!app::parse_common_options(cl, cfg)) return 2;
    if (cfg.frames == 0) cfg.frames = 1;

    const app::RunResult r = app::run(cfg);
    if (!r.ok) {
        TEFRA_LOG_ERROR("bench failed: %s", r.error.c_str());
        return 1;
    }
    const char* out = cl.get("out", "-");
    if (!app::write_bench_json(out, cfg, r)) {
        TEFRA_LOG_ERROR("cannot write %s", out);
        return 1;
    }
    if (r.validation_errors > 0) {
        TEFRA_LOG_ERROR("%u Vulkan validation errors", r.validation_errors);
        return 3;
    }
    return 0;
}
