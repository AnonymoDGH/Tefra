// tefra_run: runs a .tefra place headless with the game runtime (the same one as Studio's Play and the player):
// scripts, Players and the character, physics, humanoids, UI and sound systems. Used by the scripting tests (ctest
// label scripting) and to check a place without a window.
//
//   tefra_run --place x.tefra [--frames 60] [--dt 0.016667] [--out result.tefra] [--require-scripts]
//             [--managed DIR] [--press KEY:FRAME[:HOLD][,...]] [--expect-file FILE] [--no-character]
//
//   --press     synthetic keyboard input: KEY (tefra::Key name, e.g. E or Space) goes down at FRAME and up HOLD frames
//               later (default 1). InputBegan / InputEnded fire through UserInputService.
//   --expect-file  one text per line; every line must appear in the output log, or the run fails (exit 2).
//
// Exit codes: 0 ok, 1 error (bad place, the game could not start, cannot write), 2 an expected line is missing,
// 77 scripting unavailable while --require-scripts is set (ctest treats it as skipped).

#include "tefra/core/cli.h"
#include "tefra/core/input.h"
#include "tefra/core/log.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/scripting/script_host.h"
#include "tefra/world/datamodel.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace tefra;

namespace {

std::vector<std::string> g_log; // everything the scripts and the runtime printed, for --expect-file

void print_usage() {
    std::printf("tefra_run --place FILE.tefra [--frames N] [--dt SECONDS] [--out FILE.tefra] [--require-scripts]\n"
                "          [--managed DIR] [--press KEY:FRAME[:HOLD][,...]] [--expect-file FILE] [--no-character]\n"
                "  Runs a place headless with the game runtime (scripts, physics, character) for N frames.\n");
}

void print_output(LogLevel level, std::string_view message) {
    g_log.emplace_back(message);
    std::FILE* out = static_cast<int>(level) >= static_cast<int>(LogLevel::Warn) ? stderr : stdout;
    std::fprintf(out, "%.*s\n", static_cast<int>(message.size()), message.data());
}

// One synthetic key press from --press.
struct Press {
    Key key = Key::Unknown;
    i64 frame = 0;
    i64 hold = 1;
};

// Parses "E:30:5,Space:60". Returns false with a message on bad syntax or an unknown key name.
bool parse_presses(const std::string& text, std::vector<Press>& out, std::string* err) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        start = end + 1;
        if (item.empty()) continue;
        const std::size_t c1 = item.find(':');
        if (c1 == std::string::npos) {
            *err = "bad --press entry '" + item + "' (use KEY:FRAME[:HOLD])";
            return false;
        }
        Press p;
        p.key = key_from_name(item.substr(0, c1));
        if (p.key == Key::Unknown) {
            *err = "unknown key '" + item.substr(0, c1) + "' in --press";
            return false;
        }
        const std::size_t c2 = item.find(':', c1 + 1);
        p.frame = std::stoll(item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
        if (c2 != std::string::npos) p.hold = std::stoll(item.substr(c2 + 1));
        out.push_back(p);
    }
    return true;
}

// The synthetic input of one frame: keys held by the presses that cover it.
InputState input_for_frame(const std::vector<Press>& presses, i64 frame) {
    InputState in;
    in.keyboard = true;
    for (const Press& p : presses) {
        const usize k = static_cast<usize>(p.key);
        if (frame >= p.frame && frame < p.frame + p.hold) in.down[k] = true;
        if (frame == p.frame) in.pressed[k] = true;
        if (frame == p.frame + p.hold) in.released[k] = true;
    }
    return in;
}

bool expectations_met(const std::string& path, std::string* missing) {
    std::ifstream file(path);
    if (!file) {
        *missing = "cannot read " + path;
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        bool found = false;
        for (const std::string& l : g_log)
            if (l.find(line) != std::string::npos) {
                found = true;
                break;
            }
        if (!found) {
            *missing = line;
            return false;
        }
    }
    return true;
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
    std::vector<Press> presses;
    if (const char* p = cl.get("press")) {
        std::string err;
        if (!parse_presses(p, presses, &err)) {
            std::fprintf(stderr, "tefra_run: %s\n", err.c_str());
            return 1;
        }
    }

    world::DataModel model;
    std::string error;
    if (!model.load(place, &error)) {
        std::fprintf(stderr, "tefra_run: cannot load %s: %s\n", place, error.c_str());
        return 1;
    }

    scripting::ScriptHost host;
    std::string init_error;
    const bool scripts_ok = host.init(&init_error, cl.get("managed", ""));
    if (!scripts_ok) {
        if (cl.has("require-scripts")) {
            std::fprintf(stderr, "tefra_run: scripting unavailable: %s\n", init_error.c_str());
            return 77;
        }
        std::fprintf(stderr, "tefra_run: scripting disabled: %s\n", init_error.c_str());
    }
    host.set_log_callback(print_output);

    runtime::RuntimeConfig cfg;
    cfg.scripts = scripts_ok ? &host : nullptr;
    cfg.spawn_character = !cl.has("no-character");
    cfg.project_dir = std::filesystem::path(place).parent_path().string();
    runtime::GameRuntime game(model, cfg);
    if (!game.start()) {
        std::fprintf(stderr, "tefra_run: the game could not start\n");
        return 1;
    }
    for (i64 f = 0; f < frames; ++f) game.step(dt, input_for_frame(presses, f));
    game.stop();
    host.stop();

    if (const char* out = cl.get("out")) {
        if (!model.save(out)) {
            std::fprintf(stderr, "tefra_run: cannot write %s\n", out);
            return 1;
        }
        std::printf("saved %s\n", out);
    }
    if (const char* expect = cl.get("expect-file")) {
        std::string missing;
        if (!expectations_met(expect, &missing)) {
            std::fprintf(stderr, "tefra_run: expected output missing: %s\n", missing.c_str());
            return 2;
        }
    }
    std::printf("tefra_run: %lld frames, dt %.6f, scripting %s\n", static_cast<long long>(frames),
                static_cast<double>(dt), scripts_ok ? "on" : "off");
    return 0;
}
