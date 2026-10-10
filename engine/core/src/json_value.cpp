#include "tefra/core/json_value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tefra {

namespace {
const JsonValue kNull;
const std::string kEmpty;

struct Parser {
    std::string_view s;
    usize i = 0;
    std::string err;
    int depth = 0;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool fail(const char* msg) {
        if (err.empty()) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s at offset %zu", msg, i);
            err = buf;
        }
        return false;
    }
    bool lit(const char* w) {
        usize n = 0;
        while (w[n]) ++n;
        if (s.substr(i, n) != std::string_view(w, n)) return fail("invalid literal");
        i += n;
        return true;
    }
    static void put_utf8(std::string& o, u32 cp) {
        if (cp < 0x80) {
            o += char(cp);
        } else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(u32& v) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= u32(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= u32(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= u32(c - 'A' + 10);
            else
                return fail("bad hex digit");
        }
        return true;
    }
    bool string(std::string& o) {
        ++i; // opening quote
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                u32 cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00) {
                    u32 lo = 0;
                    if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u') return fail("lone surrogate");
                    i += 2;
                    if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                put_utf8(o, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(JsonValue& out) {
        if (++depth > 256) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        bool ok = true;
        if (c == '{') {
            out = JsonValue::object();
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
            } else {
                for (;;) {
                    ws();
                    if (i >= s.size() || s[i] != '"') {
                        ok = fail("expected key");
                        break;
                    }
                    std::string key;
                    if (!string(key)) {
                        ok = false;
                        break;
                    }
                    ws();
                    if (i >= s.size() || s[i] != ':') {
                        ok = fail("expected ':'");
                        break;
                    }
                    ++i;
                    JsonValue v;
                    if (!value(v)) {
                        ok = false;
                        break;
                    }
                    out.set(key, std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}') {
                        ++i;
                        break;
                    }
                    ok = fail("expected ',' or '}'");
                    break;
                }
            }
        } else if (c == '[') {
            out = JsonValue::array();
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
            } else {
                for (;;) {
                    JsonValue v;
                    if (!value(v)) {
                        ok = false;
                        break;
                    }
                    out.push(std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']') {
                        ++i;
                        break;
                    }
                    ok = fail("expected ',' or ']'");
                    break;
                }
            }
        } else if (c == '"') {
            std::string str;
            ok = string(str);
            out = JsonValue(std::move(str));
        } else if (c == 't') {
            ok = lit("true");
            out = JsonValue(true);
        } else if (c == 'f') {
            ok = lit("false");
            out = JsonValue(false);
        } else if (c == 'n') {
            ok = lit("null");
            out = JsonValue();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const usize start = i;
            if (s[i] == '-') ++i;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                    s[i] == '+' || s[i] == '-'))
                ++i;
            const std::string num(s.substr(start, i - start));
            char* end = nullptr;
            const f64 v = std::strtod(num.c_str(), &end);
            if (end != num.c_str() + num.size()) ok = fail("bad number");
            out = JsonValue(v);
        } else {
            ok = fail("unexpected character");
        }
        --depth;
        return ok;
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}
} // namespace

const std::string& JsonValue::as_string() const { return type_ == Type::String ? s_ : kEmpty; }

const JsonValue& JsonValue::at(usize i) const { return (type_ == Type::Array && i < arr_.size()) ? arr_[i] : kNull; }

JsonValue& JsonValue::push(JsonValue v) {
    if (type_ == Type::Null) type_ = Type::Array;
    arr_.push_back(std::move(v));
    return arr_.back();
}

const JsonValue* JsonValue::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const Member& m : obj_)
        if (m.first == key) return &m.second;
    return nullptr;
}

const JsonValue& JsonValue::operator[](std::string_view key) const {
    const JsonValue* v = find(key);
    return v ? *v : kNull;
}

JsonValue& JsonValue::set(std::string_view key, JsonValue v) {
    if (type_ == Type::Null) type_ = Type::Object;
    for (Member& m : obj_)
        if (m.first == key) {
            m.second = std::move(v);
            return m.second;
        }
    obj_.emplace_back(std::string(key), std::move(v));
    return obj_.back().second;
}

void JsonValue::dump_to(std::string& out, bool pretty, int depth) const {
    auto nl = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append(usize(d) * 2, ' ');
    };
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += b_ ? "true" : "false"; break;
    case Type::Number: {
        if (!std::isfinite(n_)) {
            out += "null";
        } else if (n_ == std::floor(n_) && std::fabs(n_) < 9.0e15) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n_));
            out += buf;
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.9g", n_);
            out += buf;
        }
        break;
    }
    case Type::String: escape(out, s_); break;
    case Type::Array:
        out += '[';
        for (usize k = 0; k < arr_.size(); ++k) {
            if (k) out += ',';
            nl(depth + 1);
            arr_[k].dump_to(out, pretty, depth + 1);
        }
        if (!arr_.empty()) nl(depth);
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (usize k = 0; k < obj_.size(); ++k) {
            if (k) out += ',';
            nl(depth + 1);
            escape(out, obj_[k].first);
            out += pretty ? ": " : ":";
            obj_[k].second.dump_to(out, pretty, depth + 1);
        }
        if (!obj_.empty()) nl(depth);
        out += '}';
        break;
    }
}

std::string JsonValue::dump(bool pretty) const {
    std::string out;
    dump_to(out, pretty, 0);
    return out;
}

bool JsonValue::parse(std::string_view text, JsonValue& out, std::string* error) {
    Parser p{text};
    JsonValue v;
    bool ok = p.value(v);
    if (ok) {
        p.ws();
        if (p.i != text.size()) ok = p.fail("trailing characters");
    }
    if (!ok) {
        if (error) *error = p.err;
        return false;
    }
    out = std::move(v);
    return true;
}

} // namespace tefra
