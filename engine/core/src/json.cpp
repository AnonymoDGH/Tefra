#include "tefra/core/json.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace tefra {

void JsonWriter::newline() {
    if (!pretty_) return;
    out_ += '\n';
    out_.append(static_cast<usize>(depth_) * 2, ' ');
}

void JsonWriter::prefix(const char* key) {
    if (!first_) out_ += ',';
    if (depth_ > 0) newline();
    first_ = false;
    if (key) {
        write_string(key);
        out_ += pretty_ ? ": " : ":";
    }
}

void JsonWriter::write_string(const char* s) {
    out_ += '"';
    for (const char* p = s; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out_ += buf;
            } else {
                out_ += static_cast<char>(c);
            }
        }
    }
    out_ += '"';
}

void JsonWriter::write_number(f64 v) {
    if (!std::isfinite(v)) {
        out_ += "null";
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    out_ += buf;
}

JsonWriter& JsonWriter::begin_object(const char* key) {
    prefix(key);
    out_ += '{';
    ++depth_;
    first_ = true;
    return *this;
}

JsonWriter& JsonWriter::end_object() {
    --depth_;
    if (!first_) newline();
    out_ += '}';
    first_ = false;
    if (depth_ == 0 && pretty_) out_ += '\n';
    return *this;
}

JsonWriter& JsonWriter::begin_array(const char* key) {
    prefix(key);
    out_ += '[';
    ++depth_;
    first_ = true;
    return *this;
}

JsonWriter& JsonWriter::end_array() {
    --depth_;
    if (!first_) newline();
    out_ += ']';
    first_ = false;
    return *this;
}

JsonWriter& JsonWriter::kv(const char* key, const char* v) {
    prefix(key);
    write_string(v ? v : "");
    return *this;
}

JsonWriter& JsonWriter::kv(const char* key, f64 v) {
    prefix(key);
    write_number(v);
    return *this;
}

JsonWriter& JsonWriter::kv(const char* key, i64 v) {
    prefix(key);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRId64, v);
    out_ += buf;
    return *this;
}

JsonWriter& JsonWriter::kv(const char* key, u64 v) {
    prefix(key);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRIu64, v);
    out_ += buf;
    return *this;
}

JsonWriter& JsonWriter::kv(const char* key, bool v) {
    prefix(key);
    out_ += v ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::value(const char* v) { return kv(nullptr, v); }
JsonWriter& JsonWriter::value(f64 v) { return kv(nullptr, v); }
JsonWriter& JsonWriter::value(i64 v) { return kv(nullptr, v); }

} // namespace tefra
