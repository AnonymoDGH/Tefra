// tefra_run: loads a .tefra place, runs its C# scripts headless for N frames and optionally saves the result.
//
//   tefra_run --place x.tefra [--frames 60] [--dt 0.016667] [--out result.tefra] [--require-scripts]
//
// Exit codes: 0 ok, 1 error (bad place, scripts could not start, cannot write), 77 scripting unavailable while
// --require-scripts is set (ctest treats it as skipped).

#include "tefra/core/cli.h"
#include "tefra/core/log.h"
#include "tefra/scripting/script_host.h"
#include "tefra/world/datamodel.h"

#include <cstdio>
#include <string>
#include <string_view>

using namespace tefra;

namespace {

void print_usage() {
    std::printf("tefra_run --place FILE.tefra [--frames N] [--dt SECONDS] [--out FILE.tefra] [--require-scripts]\n"
                "                 [--managed DIR]  (default: managed/ next to the executable)\n"
                "  Runs the Scripts of a place headless (Start, then Update each frame) and optionally saves it.\n");
}

void print_output(LogLevel level, std::string_view message) {
    std::FILE* out = static_cast<int>(level) >= static_cast<int>(LogLevel::Warn) ? stderr : stdout;
    std::fprintf(out, "%.*s\n", static_cast<int>(message.size()), message.data());
}

} // namespace

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        print_usage();
        return 0;
    }
    const char* place = cl.get("place");
    if (!place) {
        print_usage();
        return 1;
    }
    const i64 frames = cl.get_int("frames", 60);
    const f32 dt = static_cast<f32>(cl.get_float("dt", 1.0 / 60.0));

    world::DataModel model;
    std::string error;
    if (!model.load(place, &error)) {
        std::fprintf(stderr, "tefra_run: cannot load %s: %s\n", place, error.c_str());
        return 1;
    }

    scripting::ScriptHost host;
    std::string init_error;
    if (!host.init(&init_error, cl.get("managed", ""))) {
        if (cl.has("require-scripts")) {
            std::fprintf(stderr, "tefra_run: scripting unavailable: %s\n", init_error.c_str());
            return 77;
        }
        std::fprintf(stderr, "tefra_run: scripting disabled: %s\n", init_error.c_str());
    } else {
        host.set_log_callback(print_output);
        if (!host.start(model)) {
            std::fprintf(stderr, "tefra_run: the scripts could not start\n");
            return 1;
        }
        for (i64 f = 0; f < frames; ++f) host.step(dt);
        host.stop();
    }

    if (const char* out = cl.get("out")) {
        if (!model.save(out)) {
            std::fprintf(stderr, "tefra_run: cannot write %s\n", out);
            return 1;
        }
        std::printf("saved %s\n", out);
    }
    std::printf("tefra_run: %lld frames, dt %.6f, scripting %s\n", static_cast<long long>(frames),
                static_cast<double>(dt), host.available() ? "on" : "off");
    return 0;
}
