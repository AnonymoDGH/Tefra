// tefra_mcp_demo: headless MCP server over a Baseplate place (no window, no GPU). Lets an MCP client try the tools
// without Tefra Studio, and is what the ctest end-to-end test drives.
//
//   tefra_mcp_demo [--port N] [--place file.tefra]
//
// --port 0 picks a free port. The first stdout line is TEFRA_MCP_PORT=<port>; diagnostics go to stderr.

#include "tefra/core/json_value.h"
#include "tefra/mcp/server.h"
#include "tefra/mcp/world_tools.h"
#include "tefra/world/datamodel.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace tefra;
using namespace tefra::world;

namespace {

std::atomic<bool> g_quit{false};
void on_signal(int) { g_quit.store(true); }

// Roblox-style starting place: a 512 x 512 grass baseplate with its top at y = 0, and a spawn pad on it.
void build_baseplate(DataModel& dm) {
    Instance* ws = dm.workspace();
    const EnumInfo* material = ClassRegistry::get().find_enum("Material");
    const i32 grass = material ? material->index_of("Grass") : -1;

    Instance* base = dm.create("Part", ws, "Baseplate");
    base->set("Size", Value::vector3({512, 20, 512}));
    base->set("Position", Value::vector3({0, -10, 0}));
    base->set("Anchored", Value::boolean(true));
    base->set("Color", Value::color3({0.36f, 0.45f, 0.27f}));
    base->set("Material", Value::enumeration(grass >= 0 ? u32(grass) : 0u));

    Instance* spawn = dm.create("SpawnLocation", ws, "SpawnLocation");
    spawn->set("Size", Value::vector3({12, 1, 12}));
    spawn->set("Position", Value::vector3({0, 0.5f, 0}));
    spawn->set("Anchored", Value::boolean(true));
    spawn->set("Color", Value::color3({0.85f, 0.85f, 0.85f}));
}

void usage() { std::fprintf(stderr, "usage: tefra_mcp_demo [--port N] [--place file.tefra]\n"); }

} // namespace

int main(int argc, char** argv) {
    u16 port = 44755;
    std::string place;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<u16>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--place" && i + 1 < argc) {
            place = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    DataModel dm;
    if (place.empty()) {
        build_baseplate(dm);
    } else {
        std::string err;
        if (!dm.load(place.c_str(), &err)) {
            std::fprintf(stderr, "tefra_mcp_demo: cannot load %s: %s\n", place.c_str(), err.c_str());
            return 1;
        }
    }

    mcp::Server server;
    server.set_server_info("tefra-studio", "0.0.1-demo",
                           "Tefra Studio (demo). The place is a DataModel: services such as Workspace hold the "
                           "instances. Start with get_tree, look at classes with list_classes, and edit with "
                           "create_instance and set_properties.");
    const int tools = mcp::register_world_tools(server, dm);
    // Example of an extra tool: this is how Studio adds its own (play, output, capture) with the same API.
    server.add_tool("echo", "Returns the message it receives. Use it to check the connection.",
                    R"({"type":"object","properties":{"message":{"type":"string"}},"required":["message"]})",
                    [](const JsonValue& args) { return mcp::ToolResult::text(args["message"].as_string()); });

    if (!server.start(port)) {
        std::fprintf(stderr, "tefra_mcp_demo: %s\n", server.last_error().c_str());
        return 1;
    }
    std::printf("TEFRA_MCP_PORT=%u\n", static_cast<unsigned>(server.port()));
    std::fflush(stdout);
    std::fprintf(stderr, "tefra_mcp_demo: %d world tools + echo, http://127.0.0.1:%u/mcp (Ctrl+C to stop)\n", tools,
                 static_cast<unsigned>(server.port()));

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!g_quit.load()) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    server.stop();
    return 0;
}
