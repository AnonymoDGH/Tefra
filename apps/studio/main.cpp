// tefra_studio: the Tefra game editor (spec M3). Usage:
//   tefra_studio [place.tefra] [--demo | --template ID] [--validation] [--width N --height N]
//                [--capture out.png --frames N [--capture-viewport]] [--play] [--mcp-port N | --no-mcp]
// Without a place, template or demo it opens on the start page.
#include "tefra/studio/app.h"

#include "tefra/core/cli.h"
#include "tefra/core/crash.h"
#include "tefra/core/fs.h"
#include "tefra/core/log.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    // Log file next to the user's settings (send it when reporting a problem) and a crash reporter.
    const std::string log_dir = path_join(config_dir(), "logs");
    make_dirs(log_dir.c_str());
    log_set_file(path_join(log_dir, "tefra_studio.log").c_str());
    install_crash_handler();
    if (cl.has("help")) {
        std::printf("tefra_studio [place.tefra] [--demo | --template ID] [--validation] [--width N --height N] "
                    "[--capture out.png --frames N [--capture-viewport]] [--play] [--mcp-port N | --no-mcp]\n"
                    "templates:");
        for (const studio::TemplateInfo& t : studio::templates()) std::printf(" %s", t.id);
        std::printf("\n");
        return 0;
    }
    studio::StudioConfig cfg;
    cfg.width = static_cast<u32>(cl.get_int("width", 1600));
    cfg.height = static_cast<u32>(cl.get_int("height", 900));
    cfg.validation = cl.has("validation");
    cfg.gpu_index = static_cast<i32>(cl.get_int("gpu", -1));
    if (cfg.gpu_index < 0)
        if (const char* g = std::getenv("TEFRA_GPU")) cfg.gpu_index = std::atoi(g);
    cfg.demo = cl.has("demo");
    cfg.play_at_start = cl.has("play");
    cfg.mcp_port = cl.has("no-mcp") ? u16(0) : static_cast<u16>(cl.get_int("mcp-port", 44755));
    if (const char* c = cl.get("capture")) cfg.capture_path = c;
    cfg.frames = static_cast<u32>(cl.get_int("frames", cfg.capture_path.empty() ? 0 : 30));
    if (const char* t = cl.get("template")) cfg.start_template = t;
    cfg.capture_viewport = cl.has("capture-viewport");
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() > 6 && a.compare(a.size() - 6, 6, ".tefra") == 0) cfg.open_path = a;
    }
    cfg.show_home = cfg.open_path.empty() && !cfg.demo && cfg.start_template.empty() && !cfg.play_at_start;
    studio::App app(cfg);
    std::string err;
    if (!app.init(&err)) {
        TEFRA_LOG_ERROR("Tefra Studio: %s", err.c_str());
        return 1;
    }
    return app.run();
}
