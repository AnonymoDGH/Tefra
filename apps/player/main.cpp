// tefra_player: runs an exported Tefra game (spec M3b §2, ADR-018). It looks for game.json next to the executable
// (or in the folder / .tefra given on the command line):
//   { "title": "My Game", "place": "game.tefra", "width": 1280, "height": 720, "fullscreen": false }
// Usage: tefra_player [folder | place.tefra] [--fullscreen] [--width N --height N] [--validation]
//                     [--capture out.png --frames N]
#include "tefra/studio/app.h"

#include "tefra/core/cli.h"
#include "tefra/core/fs.h"
#include "tefra/core/json_value.h"
#include "tefra/core/log.h"

#include <string>
#include <vector>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    std::string dir = exe_dir(), place;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--width" || a == "--height" || a == "--capture" || a == "--frames") {
            ++i; // skip the option's value
            continue;
        }
        if (a.rfind("--", 0) == 0) continue;
        if (is_dir(a))
            dir = a;
        else if (path_extension(a) == ".tefra") {
            place = a;
            dir = path_parent(a);
        }
    }
    studio::StudioConfig cfg;
    cfg.player = true;
    cfg.show_home = false;
    cfg.play_at_start = true;
    cfg.mcp_port = 0;
    cfg.title = "Tefra Game";
    cfg.width = 1280;
    cfg.height = 720;
    std::vector<u8> data;
    JsonValue game;
    if (file_read_all(path_join(dir, "game.json").c_str(), data))
        JsonValue::parse(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), game);
    if (game.is_object()) {
        cfg.title = game["title"].as_string().empty() ? cfg.title : game["title"].as_string();
        cfg.width = static_cast<u32>(game["width"].as_int(1280));
        cfg.height = static_cast<u32>(game["height"].as_int(720));
        cfg.fullscreen = game["fullscreen"].as_bool(false);
        if (place.empty() && !game["place"].as_string().empty()) place = path_join(dir, game["place"].as_string());
    }
    if (place.empty()) place = path_join(dir, "game.tefra");
    cfg.open_path = place;
    cfg.width = static_cast<u32>(cl.get_int("width", static_cast<i64>(cfg.width)));
    cfg.height = static_cast<u32>(cl.get_int("height", static_cast<i64>(cfg.height)));
    cfg.fullscreen = cfg.fullscreen || cl.has("fullscreen");
    cfg.validation = cl.has("validation");
    if (const char* c = cl.get("capture")) cfg.capture_path = c;
    cfg.frames = static_cast<u32>(cl.get_int("frames", cfg.capture_path.empty() ? 0 : 60));
    if (!file_exists(place.c_str())) {
        TEFRA_LOG_ERROR("tefra_player: no game found (%s)", place.c_str());
        return 1;
    }
    studio::App app(cfg);
    std::string err;
    if (!app.init(&err)) {
        TEFRA_LOG_ERROR("tefra_player: %s", err.c_str());
        return 1;
    }
    return app.run();
}
