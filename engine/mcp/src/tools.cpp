#include "tefra/mcp/tools.h"

#include "tefra/core/log.h"

#include <cmath>

namespace tefra::mcp {

ToolResult ToolResult::text(std::string s) {
    ToolResult r;
    r.content.push_back(ToolContent{ToolContent::Kind::Text, std::move(s), {}});
    return r;
}

ToolResult ToolResult::json(const JsonValue& v) { return text(v.dump()); }

ToolResult ToolResult::error(std::string message) {
    ToolResult r = text(std::move(message));
    r.is_error = true;
    return r;
}

ToolResult ToolResult::image(std::string base64_data, std::string mime_type) {
    ToolResult r;
    r.content.push_back(ToolContent{ToolContent::Kind::Image, std::move(base64_data), std::move(mime_type)});
    return r;
}

JsonValue ToolResult::to_json() const {
    JsonValue items = JsonValue::array();
    for (const ToolContent& c : content) {
        JsonValue item = JsonValue::object();
        if (c.kind == ToolContent::Kind::Text) {
            item.set("type", "text");
            item.set("text", c.text);
        } else {
            item.set("type", "image");
            item.set("data", c.text);
            item.set("mimeType", c.mime_type);
        }
        items.push(std::move(item));
    }
    JsonValue out = JsonValue::object();
    out.set("content", std::move(items));
    out.set("isError", is_error);
    return out;
}

namespace {

std::string json_kind(const JsonValue& v) {
    switch (v.type()) {
    case JsonValue::Type::Null: return "null";
    case JsonValue::Type::Bool: return "boolean";
    case JsonValue::Type::Number: return "number";
    case JsonValue::Type::String: return "string";
    case JsonValue::Type::Array: return "array";
    case JsonValue::Type::Object: return "object";
    }
    return "unknown";
}

bool type_matches(std::string_view type, const JsonValue& v) {
    if (type == "string") return v.is_string();
    if (type == "number") return v.is_number();
    if (type == "integer") return v.is_number() && std::floor(v.as_number()) == v.as_number();
    if (type == "boolean") return v.is_bool();
    if (type == "object") return v.is_object();
    if (type == "array") return v.is_array();
    if (type == "null") return v.is_null();
    return true; // unknown type keyword: do not reject
}

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

} // namespace

std::string validate_arguments(const JsonValue& schema, const JsonValue& arguments) {
    if (!arguments.is_object()) return "arguments must be a JSON object";
    const JsonValue& required = schema["required"];
    for (usize i = 0; i < required.size(); ++i) {
        const JsonValue& key = required.at(i);
        if (key.is_string() && arguments[key.as_string()].is_null())
            return "missing required argument '" + key.as_string() + "'";
    }
    const JsonValue& props = schema["properties"];
    for (const auto& [key, value] : arguments.members()) {
        const JsonValue* prop = props.find(key);
        if (!prop || value.is_null()) continue;
        const JsonValue& type = (*prop)["type"];
        if (type.is_string() && !type_matches(type.as_string(), value))
            return "argument '" + key + "' must be of type " + type.as_string() + " (got " + json_kind(value) + ")";
        const JsonValue& allowed = (*prop)["enum"];
        if (allowed.is_array()) {
            bool found = false;
            for (const JsonValue& option : allowed.items())
                if (option.dump() == value.dump()) found = true;
            if (!found) return "argument '" + key + "' must be one of the allowed values: " + allowed.dump();
        }
    }
    return {};
}

bool ToolRegistry::add(std::string name, std::string description, std::string_view input_schema_json,
                       ToolHandler handler, std::string* error) {
    auto fail = [&](std::string why) {
        TEFRA_LOG_ERROR("mcp: tool '%s' rejected: %s", name.c_str(), why.c_str());
        if (error) *error = std::move(why);
        return false;
    };
    if (!valid_name(name)) return fail("invalid tool name (use letters, digits, '_', '-', '.')");
    if (!handler) return fail("no handler");
    JsonValue schema;
    std::string parse_error;
    if (!JsonValue::parse(input_schema_json, schema, &parse_error)) return fail("schema is not JSON: " + parse_error);
    if (!schema.is_object() || schema["type"].as_string() != "object")
        return fail("inputSchema must be a JSON object with \"type\": \"object\"");
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& t : tools_)
        if (t->name == name) return fail("a tool with this name already exists");
    auto def = std::make_shared<ToolDef>();
    def->name = std::move(name);
    def->description = std::move(description);
    def->input_schema = std::move(schema);
    def->handler = std::move(handler);
    tools_.push_back(std::move(def));
    if (error) error->clear();
    return true;
}

std::shared_ptr<const ToolDef> ToolRegistry::find(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& t : tools_)
        if (t->name == name) return t;
    return nullptr;
}

std::vector<std::shared_ptr<const ToolDef>> ToolRegistry::list() const {
    std::lock_guard<std::mutex> lock(mu_);
    return tools_;
}

usize ToolRegistry::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return tools_.size();
}

} // namespace tefra::mcp
