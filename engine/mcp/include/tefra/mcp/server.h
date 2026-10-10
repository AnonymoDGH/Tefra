#pragma once

// MCP server for Tefra Studio (spec M3, ADR-015): JSON-RPC 2.0 over the Streamable HTTP transport of MCP protocol
// 2025-06-18, bound to 127.0.0.1 only, at POST /mcp.
//
// Threading. start() launches a listener thread; it accepts connections (one thread each) and parses requests there.
// Tool handlers never run on those threads. A request waits (condition variable, timeout) until the engine calls
// poll() on its own thread, the editor's main thread, where the DataModel may be touched safely. start(), poll() and
// stop() must be called from that same thread.
//
// Adding a tool (Studio registers its own the same way, next to register_world_tools):
//
//     mcp::Server server;
//     server.start(44755);
//     server.add_tool(
//         "capture_viewport", "Returns a PNG of the 3D viewport.",
//         R"({"type":"object","properties":{"width":{"type":"integer","minimum":16}}})",
//         [&](const JsonValue& args) -> mcp::ToolResult {
//             if (!studio.has_frame()) return mcp::ToolResult::error("Play is not running.");
//             return mcp::ToolResult::image(studio.frame_png_base64(args["width"].as_int(640)));
//         });
//     // in the main loop, once per frame:
//     server.poll();
//
// Handlers return ToolResult::json(...) for structured data, ToolResult::text(...) for plain text,
// ToolResult::image(...) for pictures, and ToolResult::error(...) for problems the model can fix. Never throw:
// the build has no exceptions.

#include "tefra/core/types.h"
#include "tefra/mcp/jsonrpc.h"
#include "tefra/mcp/tools.h"

#include <memory>
#include <string>
#include <string_view>

namespace tefra::mcp {

class Server {
  public:
    Server();
    ~Server(); // stops the server
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Name, version and instructions reported by initialize. Call before start().
    void set_server_info(std::string name, std::string version, std::string instructions = {});

    // How long an HTTP request waits for poll() before it fails with an error result (default 15 s).
    void set_request_timeout_ms(u32 ms);

    // Registers a tool. input_schema_json must be a JSON object with "type": "object". Returns false (and logs why)
    // for an invalid or duplicate tool. Tools may be added while the server runs.
    bool add_tool(std::string name, std::string description, std::string_view input_schema_json, ToolHandler handler);

    // Binds 127.0.0.1:port and starts the listener thread. port 0 picks a free port (see port()). False if the socket
    // cannot be bound; last_error() says why.
    bool start(u16 port);
    void stop(); // cancels queued calls, closes connections, joins the threads. Idempotent.
    bool running() const;
    u16 port() const; // bound port while running, else 0
    const std::string& last_error() const;
    usize tool_count() const;

    // Runs queued tool calls on the calling thread (at most max_calls). Returns how many ran. Cheap when idle.
    usize poll(usize max_calls = 64);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tefra::mcp
