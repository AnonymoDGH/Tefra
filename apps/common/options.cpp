#include "options.h"

#include "tefra/core/log.h"

#include <cstdio>

namespace tefra::app {

bool parse_common_options(const CommandLine& cl, RunConfig& cfg) {
    cfg.width = static_cast<u32>(cl.get_int("width", cfg.width));
    cfg.height = static_cast<u32>(cl.get_int("height", cfg.height));
    cfg.frames = static_cast<u32>(cl.get_int("frames", cfg.frames));
    cfg.warmup = static_cast<u32>(cl.get_int("warmup", cfg.warmup));
    cfg.gpu = static_cast<i32>(cl.get_int("gpu", cfg.gpu));
#if defined(NDEBUG)
    cfg.validation = cl.has("validation");
#else
    cfg.validation = !cl.has("no-validation");
#endif
    cfg.strict_baseline = cl.has("strict");
    if (const char* pm = cl.get("present")) {
        if (!rhi::parse_present_mode(pm, cfg.present)) {
            TEFRA_LOG_ERROR("invalid --present '%s' (fifo|mailbox|immediate)", pm);
            return false;
        }
    }
    if (const char* c = cl.get("capture")) cfg.capture_path = c;
    if (const char* t = cl.get("tier")) cfg.tier = t;
    cfg.animate = cl.has("animate");
    if (cfg.width == 0 || cfg.height == 0 || cfg.width > 8192 || cfg.height > 8192) {
        TEFRA_LOG_ERROR("invalid resolution %ux%u", cfg.width, cfg.height);
        return false;
    }
    return true;
}

void print_common_usage() {
    std::printf("  --width N --height N    resolution (default 1280x720)\n"
                "  --frames N --warmup N   frames to render / to discard from statistics\n"
                "  --gpu N                 adapter index (see tefra_info)\n"
                "  --present MODE          fifo | mailbox | immediate (window mode)\n"
                "  --capture FILE.png      save the last frame\n"
                "  --validation            enable Vulkan validation (default on in Debug; --no-validation)\n"
                "  --strict                only baseline features of the reference hardware\n"
                "  --animate               move the camera/time between frames\n");
}

} // namespace tefra::app
