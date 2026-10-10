#pragma once

// JSON DOM: parse (strict RFC 8259, no exceptions) and dump. Objects keep insertion order. Used for scene files
// (.tefra), the MCP protocol and tool configuration.

#include "tefra/core/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tefra {

class JsonValue {
  public:
    enum class Type : u8 { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;
    JsonValue(bool b) : type_(Type::Bool), b_(b) {}
    JsonValue(f64 n) : type_(Type::Number), n_(n) {}
    JsonValue(i32 n) : type_(Type::Number), n_(f64(n)) {}
    JsonValue(i64 n) : type_(Type::Number), n_(f64(n)) {}
    JsonValue(u32 n) : type_(Type::Number), n_(f64(n)) {}
    JsonValue(u64 n) : type_(Type::Number), n_(f64(n)) {}
    JsonValue(const char* s) : type_(Type::String), s_(s) {}
    JsonValue(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static JsonValue array() {
        JsonValue v;
        v.type_ = Type::Array;
        return v;
    }
    static JsonValue object() {
        JsonValue v;
        v.type_ = Type::Object;
        return v;
    }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool def = false) const { return type_ == Type::Bool ? b_ : def; }
    f64 as_number(f64 def = 0.0) const { return type_ == Type::Number ? n_ : def; }
    i64 as_int(i64 def = 0) const { return type_ == Type::Number ? static_cast<i64>(n_) : def; }
    const std::string& as_string() const; // empty string when not a string

    // Arrays.
    usize size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }
    const JsonValue& at(usize i) const; // null when out of range
    JsonValue& push(JsonValue v);       // converts null to an array
    const std::vector<JsonValue>& items() const { return arr_; }

    // Objects.
    const JsonValue& operator[](std::string_view key) const; // null when missing
    const JsonValue* find(std::string_view key) const;
    JsonValue& set(std::string_view key, JsonValue v); // converts null to an object; replaces an existing key
    bool contains(std::string_view key) const { return find(key) != nullptr; }
    const std::vector<Member>& members() const { return obj_; }

    std::string dump(bool pretty = false) const;
    // Returns false (and an error with the byte offset) on malformed input.
    static bool parse(std::string_view text, JsonValue& out, std::string* error = nullptr);

  private:
    void dump_to(std::string& out, bool pretty, int depth) const;
    Type type_ = Type::Null;
    bool b_ = false;
    f64 n_ = 0.0;
    std::string s_;
    std::vector<JsonValue> arr_;
    std::vector<Member> obj_;
};

} // namespace tefra
