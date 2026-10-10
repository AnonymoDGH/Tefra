// tefra_viewer: interactive window. Right mouse = look, WASD/QE = move, Shift = fast, F12 = capture, Esc = quit.
#include "options.h"

#include "tefra/core/log.h"

#include <cstdio>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_viewer [options]\n");
        app::print_common_usage();
        return 0;
    }
    app::RunConfig cfg;
    cfg.window = true;
    cfg.fly_camera = true;
    if (!app::parse_common_options(cl, cfg)) return 2;
    const app::RunResult r = app::run(cfg);
    if (!r.ok) {
        TEFRA_LOG_ERROR("viewer failed: %s", r.error.c_str());
        return 1;
    }
    if (r.validation_errors > 0) {
        TEFRA_LOG_ERROR("%u Vulkan validation errors", r.validation_errors);
        return 3;
    }
    return 0;
}
