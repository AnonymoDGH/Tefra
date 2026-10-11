#pragma once

// JSON-RPC 2.0 and MCP method dispatch (initialize, ping, tools/list, tools/call). Pure: no sockets, no threads.
// Tool execution goes through an Executor, so the server can run it on the editor's main thread and tests can run
// it inline.

#include "tefra/mcp/tools.h"

#include <functional>
#include <optional>
#include <string>

namespace tefra::mcp {

inline constexpr const char* kProtocolVersion = "2025-06-18";

enum RpcCode : int {
    kParseError = -32700,
    kInvalidRequest = -32600,
    kMethodNotFound = -32601,
    kInvalidParams = -32602,
    kInternalError = -32603,
    kServerError = -32000,
};

struct ServerInfo {
    std::string name = "tefra-studio";
    std::string version = "0.0.1";
    std::string instructions; // shown to the model by initialize
};

// Runs a tool. The default executor calls the handler directly.
using Executor = std::function<ToolResult(const ToolDef& tool, const JsonValue& arguments)>;

// Builds the reply to one incoming message. Returns nullopt when nothing must be sent back: notifications (no "id")
// and responses to requests the server never sent. A JSON array (batch) is rejected, since protocol 2025-06-18 removed
// batching.
std::optional<JsonValue> handle_message(const JsonValue& message, ToolRegistry& tools, const ServerInfo& info,
                                        const Executor& exec);

JsonValue make_error(const JsonValue& id, int code, std::string message);

} // namespace tefra::mcp
