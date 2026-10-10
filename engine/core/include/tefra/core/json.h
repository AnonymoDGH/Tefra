#pragma once

// Streaming JSON writer (no DOM). Used for bench reports and capability dumps.

#include "tefra/core/types.h"

#include <string>

namespace tefra {

class JsonWriter {
  public:
    explicit JsonWriter(bool pretty = true) : pretty_(pretty) {}

    JsonWriter& begin_object(const char* key = nullptr);
    JsonWriter& end_object();
    JsonWriter& begin_array(const char* key = nullptr);
    JsonWriter& end_array();

    JsonWriter& kv(const char* key, const char* v);
    JsonWriter& kv(const char* key, const std::string& v) { return kv(key, v.c_str()); }
    JsonWriter& kv(const char* key, f64 v);
    JsonWriter& kv(const char* key, i64 v);
    JsonWriter& kv(const char* key, u64 v);
    JsonWriter& kv(const char* key, i32 v) { return kv(key, static_cast<i64>(v)); }
    JsonWriter& kv(const char* key, u32 v) { return kv(key, static_cast<u64>(v)); }
    JsonWriter& kv(const char* key, bool v);

    // Array elements.
    JsonWriter& value(const char* v);
    JsonWriter& value(f64 v);
    JsonWriter& value(i64 v);

    const std::string& str() const { return out_; }

  private:
    void prefix(const char* key);
    void newline();
    void write_string(const char* s);
    void write_number(f64 v);

    std::string out_;
    int depth_ = 0;
    bool first_ = true;
    bool pretty_;
};

} // namespace tefra
