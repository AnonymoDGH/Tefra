#include "tefra/mcp/jsonrpc.h"

namespace tefra::mcp {

namespace {

JsonValue reply(const JsonValue& id, JsonValue result) {
    JsonValue out = JsonValue::object();
    out.set("jsonrpc", "2.0");
    out.set("id", id);
    out.set("result", std::move(result));
    return out;
}

bool valid_id(const JsonValue& id) { return id.is_null() || id.is_string() || id.is_number(); }

} // namespace

JsonValue make_error(const JsonValue& id, int code, std::string message) {
    JsonValue err = JsonValue::object();
    err.set("code", i64(code));
    err.set("message", std::move(message));
    JsonValue out = JsonValue::object();
    out.set("jsonrpc", "2.0");
    out.set("id", id);
    out.set("error", std::move(err));
    return out;
}

std::optional<JsonValue> handle_message(const JsonValue& msg, ToolRegistry& tools, const ServerInfo& info,
                                        const Executor& exec) {
    const JsonValue no_id;
    if (msg.is_array())
        return make_error(no_id, kInvalidRequest, "batch requests are not supported (protocol 2025-06-18)");
    if (!msg.is_object()) return make_error(no_id, kInvalidRequest, "message must be a JSON object");

    const JsonValue* id_ptr = msg.find("id");
    const JsonValue id = id_ptr ? *id_ptr : JsonValue();
    if (id_ptr && !valid_id(id)) return make_error(no_id, kInvalidRequest, "id must be a string, a number or null");

    const JsonValue* version = msg.find("jsonrpc");
    if (!version || !version->is_string() || version->as_string() != "2.0")
        return make_error(id, kInvalidRequest, "jsonrpc must be \"2.0\"");

    const JsonValue* method_ptr = msg.find("method");
    if (!method_ptr) {
        // A response to a request the server never sent: nothing to do. Anything else is malformed.
        if (msg.find("result") || msg.find("error")) return std::nullopt;
        return make_error(id, kInvalidRequest, "missing method");
    }
    if (!method_ptr->is_string()) return make_error(id, kInvalidRequest, "method must be a string");
    if (!id_ptr) return std::nullopt; // notification (e.g. notifications/initialized): no reply

    const std::string& method = method_ptr->as_string();
    const JsonValue& params = msg["params"];
    if (!params.is_null() && !params.is_object()) return make_error(id, kInvalidParams, "params must be an object");

    if (method == "initialize") {
        JsonValue caps = JsonValue::object();
        caps.set("tools", JsonValue::object());
        JsonValue server = JsonValue::object();
        server.set("name", info.name);
        server.set("version", info.version);
        JsonValue result = JsonValue::object();
        result.set("protocolVersion", kProtocolVersion);
        result.set("capabilities", std::move(caps));
        result.set("serverInfo", std::move(server));
        if (!info.instructions.empty()) result.set("instructions", info.instructions);
        return reply(id, std::move(result));
    }

    if (method == "ping") return reply(id, JsonValue::object());

    if (method == "tools/list") {
        JsonValue list = JsonValue::array();
        for (const auto& t : tools.list()) {
            JsonValue entry = JsonValue::object();
            entry.set("name", t->name);
            entry.set("description", t->description);
            entry.set("inputSchema", t->input_schema);
            list.push(std::move(entry));
        }
        JsonValue result = JsonValue::object();
        result.set("tools", std::move(list));
        return reply(id, std::move(result));
    }

    if (method == "tools/call") {
        const JsonValue* name = params.find("name");
        if (!name || !name->is_string()) return make_error(id, kInvalidParams, "tools/call needs a string 'name'");
        const std::shared_ptr<const ToolDef> tool = tools.find(name->as_string());
        if (!tool) return make_error(id, kInvalidParams, "unknown tool '" + name->as_string() + "'");
        JsonValue args = params["arguments"];
        if (args.is_null()) args = JsonValue::object();
        if (!args.is_object()) return make_error(id, kInvalidParams, "arguments must be a JSON object");
        const std::string problem = validate_arguments(tool->input_schema, args);
        if (!problem.empty())
            return reply(id, ToolResult::error("Invalid arguments for '" + tool->name + "': " + problem).to_json());
        return reply(id, exec(*tool, args).to_json());
    }

    return make_error(id, kMethodNotFound, "method not found: " + method);
}

} // namespace tefra::mcp
