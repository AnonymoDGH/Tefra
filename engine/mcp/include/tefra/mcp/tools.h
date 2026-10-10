#pragma once

// MCP tools (spec 2025-06-18, "tools"): a named function with a JSON Schema for its arguments. The registry is
// thread-safe; handlers are run by Server::poll(), on the thread that owns the engine state.

#include "tefra/core/json_value.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::mcp {

// One item of a tool result: text (also used for JSON) or an image (base64 PNG, for capture tools).
struct ToolContent {
    enum class Kind : u8 { Text, Image };
    Kind kind = Kind::Text;
    std::string text;      // Text: the text. Image: base64 data.
    std::string mime_type; // Image only, e.g. "image/png".
};

// Result of a tool call. Problems the model can fix (bad arguments, unknown instance, wrong type) go in as an error
// result (is_error = true with a message), not as a JSON-RPC error, so the model sees them and can retry.
struct ToolResult {
    std::vector<ToolContent> content;
    bool is_error = false;

    static ToolResult text(std::string s);
    static ToolResult json(const JsonValue& v); // compact JSON, as one text item
    static ToolResult error(std::string message);
    static ToolResult image(std::string base64_data, std::string mime_type = "image/png");

    JsonValue to_json() const; // {"content": [...], "isError": bool}
};

using ToolHandler = std::function<ToolResult(const JsonValue& arguments)>;

struct ToolDef {
    std::string name;
    std::string description;
    JsonValue input_schema; // object with "type": "object"
    ToolHandler handler;
};

// Checks arguments against the schema: required keys, declared types and enums of top-level properties. Null values
// of optional keys count as absent. Returns "" when valid, otherwise a message for the model.
std::string validate_arguments(const JsonValue& schema, const JsonValue& arguments);

class ToolRegistry {
  public:
    // Fails (returns false, with a reason in error) on an empty or duplicate name, names outside [A-Za-z0-9_.-],
    // or a schema that is not a JSON object of type "object".
    bool add(std::string name, std::string description, std::string_view input_schema_json, ToolHandler handler,
             std::string* error = nullptr);
    std::shared_ptr<const ToolDef> find(std::string_view name) const;
    std::vector<std::shared_ptr<const ToolDef>> list() const;
    usize size() const;

  private:
    mutable std::mutex mu_;
    std::vector<std::shared_ptr<const ToolDef>> tools_;
};

} // namespace tefra::mcp
