// tefra_studio: the Tefra game editor (spec M3). Usage:
//   tefra_studio [place.tefra] [--demo] [--validation] [--width N --height N] [--capture out.png --frames N]
#include "tefra/studio/app.h"

#include "tefra/core/cli.h"
#include "tefra/core/log.h"

#include <cstdio>
#include <string>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_studio [place.tefra] [--demo] [--validation] [--width N --height N] "
                    "[--capture out.png --frames N] [--play] [--mcp-port N | --no-mcp]\n");
        return 0;
    }
    studio::StudioConfig cfg;
    cfg.width = static_cast<u32>(cl.get_int("width", 1600));
    cfg.height = static_cast<u32>(cl.get_int("height", 900));
    cfg.validation = cl.has("validation");
    cfg.demo = cl.has("demo");
    cfg.play_at_start = cl.has("play");
    cfg.mcp_port = cl.has("no-mcp") ? u16(0) : static_cast<u16>(cl.get_int("mcp-port", 44755));
    if (const char* c = cl.get("capture")) cfg.capture_path = c;
    cfg.frames = static_cast<u32>(cl.get_int("frames", cfg.capture_path.empty() ? 0 : 30));
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.size() > 6 && a.compare(a.size() - 6, 6, ".tefra") == 0) cfg.open_path = a;
    }
    studio::App app(cfg);
    std::string err;
    if (!app.init(&err)) {
        TEFRA_LOG_ERROR("Tefra Studio: %s", err.c_str());
        return 1;
    }
    return app.run();
}
