// XML place / model reader (format version 4, ADR-017). A small non-validating parser (elements, attributes, text,
// CDATA, comments, the five predefined entities and numeric character references) builds a flat node table; then the
// Item / Properties tree is walked without recursion and every property element is decoded by its tag name.
#include "internal.h"
#include "tefra/rbx/document.h"

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tefra::rbx {
namespace {

constexpr usize kMaxXmlDepth = 4096;
constexpr usize kMaxXmlNodes = 8u * 1024u * 1024u;

struct XNode {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text; // character data directly inside this element (entities decoded, CDATA included)
    std::vector<u32> children;
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool is_name_end(char c) { return is_space(c) || c == '/' || c == '>' || c == '='; }

std::string_view trim(std::string_view v) {
    while (!v.empty() && is_space(v.front())) v.remove_prefix(1);
    while (!v.empty() && is_space(v.back())) v.remove_suffix(1);
    return v;
}

void append_utf8(std::string& out, u32 cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

// Predefined entities and &#N; / &#xN; references. Unknown or malformed references are kept literally.
std::string decode_entities(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) return std::string(raw);
    std::string out;
    out.reserve(raw.size());
    usize i = 0;
    while (i < raw.size()) {
        if (raw[i] != '&') {
            out += raw[i++];
            continue;
        }
        const usize semi = raw.find(';', i);
        if (semi == std::string_view::npos || semi - i > 12) {
            out += raw[i++];
            continue;
        }
        const std::string_view ent = raw.substr(i + 1, semi - i - 1);
        bool done = true;
        if (ent == "lt") {
            out += '<';
        } else if (ent == "gt") {
            out += '>';
        } else if (ent == "amp") {
            out += '&';
        } else if (ent == "quot") {
            out += '"';
        } else if (ent == "apos") {
            out += '\'';
        } else if (!ent.empty() && ent[0] == '#') {
            std::string_view digits = ent.substr(1);
            u32 base = 10;
            if (!digits.empty() && (digits[0] == 'x' || digits[0] == 'X')) {
                base = 16;
                digits = digits.substr(1);
            }
            bool ok = !digits.empty();
            u32 cp = 0;
            for (usize k = 0; ok && k < digits.size(); ++k) {
                const char c = digits[k];
                u32 v = 0;
                if (c >= '0' && c <= '9')
                    v = static_cast<u32>(c - '0');
                else if (base == 16 && c >= 'a' && c <= 'f')
                    v = static_cast<u32>(c - 'a' + 10);
                else if (base == 16 && c >= 'A' && c <= 'F')
                    v = static_cast<u32>(c - 'A' + 10);
                else
                    ok = false;
                cp = cp * base + v;
                if (cp > 0x10FFFFu) ok = false;
            }
            if (ok)
                append_utf8(out, cp);
            else
                done = false;
        } else {
            done = false;
        }
        if (done)
            i = semi + 1;
        else
            out += raw[i++];
    }
    return out;
}

class Tree {
  public:
    std::vector<XNode> nodes; // nodes[0] is the root element
    std::string error;

    bool parse(std::string_view s);

  private:
    bool fail(std::string msg) {
        error = std::move(msg);
        return false;
    }
};

bool Tree::parse(std::string_view s) {
    usize i = 0;
    if (s.substr(0, 3) == "\xEF\xBB\xBF") i = 3; // UTF-8 byte order mark
    std::vector<u32> stack;
    bool seen_root = false;
    while (i < s.size()) {
        if (s[i] != '<') {
            const usize lt = s.find('<', i);
            const usize end = lt == std::string_view::npos ? s.size() : lt;
            const std::string_view chunk = s.substr(i, end - i);
            if (!stack.empty())
                nodes[stack.back()].text += decode_entities(chunk);
            else if (!trim(chunk).empty())
                return fail("text outside the root element");
            i = end;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) {
            const usize e = s.find("-->", i + 4);
            if (e == std::string_view::npos) return fail("unterminated comment");
            i = e + 3;
            continue;
        }
        if (s.compare(i, 9, "<![CDATA[") == 0) {
            if (stack.empty()) return fail("CDATA outside the root element");
            const usize e = s.find("]]>", i + 9);
            if (e == std::string_view::npos) return fail("unterminated CDATA section");
            nodes[stack.back()].text.append(s.substr(i + 9, e - (i + 9)));
            i = e + 3;
            continue;
        }
        if (s.compare(i, 2, "<?") == 0) {
            const usize e = s.find("?>", i + 2);
            if (e == std::string_view::npos) return fail("unterminated processing instruction");
            i = e + 2;
            continue;
        }
        if (s.compare(i, 2, "<!") == 0) { // DOCTYPE and similar: skipped
            const usize e = s.find('>', i + 2);
            if (e == std::string_view::npos) return fail("unterminated declaration");
            i = e + 1;
            continue;
        }
        if (s.compare(i, 2, "</") == 0) {
            const usize e = s.find('>', i + 2);
            if (e == std::string_view::npos) return fail("unterminated closing tag");
            const std::string_view name = trim(s.substr(i + 2, e - (i + 2)));
            if (stack.empty() || nodes[stack.back()].tag != name)
                return fail("mismatched closing tag </" + std::string(name) + ">");
            stack.pop_back();
            i = e + 1;
            continue;
        }
        // Start tag.
        usize p = i + 1;
        usize name_end = p;
        while (name_end < s.size() && !is_name_end(s[name_end])) ++name_end;
        if (name_end == p) return fail("element without a name");
        XNode node;
        node.tag = std::string(s.substr(p, name_end - p));
        p = name_end;
        bool self_closing = false;
        for (;;) {
            while (p < s.size() && is_space(s[p])) ++p;
            if (p >= s.size()) return fail("unterminated tag <" + node.tag + ">");
            if (s[p] == '>') {
                ++p;
                break;
            }
            if (s[p] == '/') {
                if (p + 1 < s.size() && s[p + 1] == '>') {
                    self_closing = true;
                    p += 2;
                    break;
                }
                return fail("malformed tag <" + node.tag + ">");
            }
            usize an = p;
            while (an < s.size() && !is_name_end(s[an])) ++an;
            if (an == p) return fail("malformed attribute in <" + node.tag + ">");
            std::string aname(s.substr(p, an - p));
            p = an;
            while (p < s.size() && is_space(s[p])) ++p;
            if (p >= s.size() || s[p] != '=') return fail("attribute without value in <" + node.tag + ">");
            ++p;
            while (p < s.size() && is_space(s[p])) ++p;
            if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) return fail("unquoted attribute in <" + node.tag + ">");
            const char quote = s[p];
            const usize ve = s.find(quote, p + 1);
            if (ve == std::string_view::npos) return fail("unterminated attribute value");
            node.attrs.emplace_back(std::move(aname), decode_entities(s.substr(p + 1, ve - p - 1)));
            p = ve + 1;
        }
        i = p;
        if (nodes.size() >= kMaxXmlNodes) return fail("too many XML elements");
        const u32 idx = static_cast<u32>(nodes.size());
        if (stack.empty()) {
            if (seen_root) return fail("more than one root element");
            seen_root = true;
        } else {
            if (stack.size() >= kMaxXmlDepth) return fail("XML nested too deeply");
            nodes[stack.back()].children.push_back(idx);
        }
        nodes.push_back(std::move(node));
        if (!self_closing) stack.push_back(idx);
    }
    if (!stack.empty()) return fail("unclosed element <" + nodes[stack.back()].tag + ">");
    if (nodes.empty()) return fail("no root element");
    return true;
}

std::string_view attr(const XNode& n, std::string_view name) {
    for (const auto& [k, v] : n.attrs)
        if (k == name) return v;
    return {};
}

const XNode* child(const Tree& t, const XNode& n, std::string_view tag) {
    for (u32 c : n.children)
        if (t.nodes[c].tag == tag) return &t.nodes[c];
    return nullptr;
}

// The URI child of a Content / Family element: "uri" or "url".
const XNode* child_uri(const Tree& t, const XNode& n) {
    if (const XNode* u = child(t, n, "uri")) return u;
    return child(t, n, "url");
}

bool parse_f64(std::string_view text, f64& out) {
    text = trim(text);
    if (text == "INF" || text == "+INF" || text == "inf") {
        out = std::numeric_limits<f64>::infinity();
        return true;
    }
    if (text == "-INF" || text == "-inf") {
        out = -std::numeric_limits<f64>::infinity();
        return true;
    }
    if (text == "NAN" || text == "-NAN" || text == "nan") {
        out = std::numeric_limits<f64>::quiet_NaN();
        return true;
    }
    if (!text.empty() && text[0] == '+') text.remove_prefix(1);
    if (text.empty()) return false;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc() && r.ptr == text.data() + text.size();
}

bool parse_i64(std::string_view text, i64& out) {
    text = trim(text);
    if (!text.empty() && text[0] == '+') text.remove_prefix(1);
    if (text.empty()) return false;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc() && r.ptr == text.data() + text.size();
}

// Numeric child element: missing reads as 0; malformed clears `ok`.
f64 num_child(const Tree& t, const XNode& n, std::string_view tag, bool& ok) {
    const XNode* c = child(t, n, tag);
    if (!c) return 0.0;
    f64 v = 0.0;
    if (!parse_f64(c->text, v)) ok = false;
    return v;
}

// Whitespace-separated numbers.
bool parse_number_list(std::string_view text, std::vector<f32>& out) {
    usize i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_space(text[i])) ++i;
        if (i >= text.size()) break;
        usize e = i;
        while (e < text.size() && !is_space(text[e])) ++e;
        f64 v = 0.0;
        if (!parse_f64(text.substr(i, e - i), v)) return false;
        out.push_back(static_cast<f32>(v));
        i = e;
    }
    return true;
}

struct Ctx {
    const Tree& t;
    const std::unordered_map<std::string, u32>& refs;
    const std::unordered_map<std::string, u32>& shared;
};

i64 lookup(const std::unordered_map<std::string, u32>& map, std::string_view key) {
    auto it = map.find(std::string(trim(key)));
    return it == map.end() ? -1 : static_cast<i64>(it->second);
}

// Reads the child elements named tags[0..count) into f[base..base+count).
void read_children(const Tree& t, const XNode& n, std::initializer_list<const char*> tags, bool& ok, f32* f) {
    usize k = 0;
    for (const char* tag : tags) f[k++] = static_cast<f32>(num_child(t, n, tag, ok));
}

// Position and rotation of a CFrame element: X, Y, Z, R00..R22 children, into f[0..11].
void read_cframe(const Tree& t, const XNode& n, bool& ok, f32* f) {
    read_children(t, n, {"X", "Y", "Z", "R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"}, ok, f);
}

// Converts one property element. Returns false (with `why`) for unknown or malformed values.
bool convert(const Ctx& c, const XNode& n, std::string_view prop_name, Value& v, std::string& why) {
    const std::string& tag = n.tag;
    bool ok = true;
    if (tag == "string" || tag == "ProtectedString") {
        v.type = tag == "string" ? Type::String : Type::ProtectedString;
        v.s = n.text;
    } else if (tag == "BinaryString") {
        v.type = Type::BinaryString;
        if (!base64_decode(n.text, v.s)) v.s.clear();
    } else if (tag == "bool") {
        v.type = Type::Bool;
        v.i = trim(n.text) == "true" ? 1 : 0;
    } else if (tag == "int") {
        i64 x = 0;
        if (!parse_i64(n.text, x)) return why = "malformed int", false;
        v.type = prop_name == "BrickColor" ? Type::BrickColor : Type::Int32;
        v.i = x;
    } else if (tag == "token") {
        i64 x = 0;
        if (!parse_i64(n.text, x)) return why = "malformed token", false;
        v.type = Type::Enum;
        v.i = x;
    } else if (tag == "int64") {
        i64 x = 0;
        if (!parse_i64(n.text, x)) return why = "malformed int64", false;
        v.type = Type::Int64;
        v.i = x;
    } else if (tag == "float" || tag == "double") {
        f64 d = 0.0;
        if (!parse_f64(n.text, d)) return why = "malformed number", false;
        v.type = tag == "float" ? Type::Float32 : Type::Float64;
        v.d = d;
    } else if (tag == "UDim") {
        v.type = Type::UDim;
        read_children(c.t, n, {"S", "O"}, ok, v.f);
    } else if (tag == "UDim2") {
        v.type = Type::UDim2;
        read_children(c.t, n, {"XS", "XO", "YS", "YO"}, ok, v.f);
    } else if (tag == "Vector2") {
        v.type = Type::Vector2;
        read_children(c.t, n, {"X", "Y"}, ok, v.f);
    } else if (tag == "Vector3" || tag == "Vector3int16") {
        v.type = tag == "Vector3" ? Type::Vector3 : Type::Vector3int16;
        read_children(c.t, n, {"X", "Y", "Z"}, ok, v.f);
    } else if (tag == "Color3") {
        v.type = Type::Color3;
        read_children(c.t, n, {"R", "G", "B"}, ok, v.f);
    } else if (tag == "Color3uint8") {
        i64 x = 0;
        if (!parse_i64(n.text, x) || x < 0 || x > 0xFFFFFFFFll) return why = "malformed Color3uint8", false;
        const u32 packed = static_cast<u32>(x);
        v.type = Type::Color3uint8;
        v.f[0] = static_cast<f32>((packed >> 16) & 0xFFu); // R (0xRRGGBB in the low 24 bits)
        v.f[1] = static_cast<f32>((packed >> 8) & 0xFFu);  // G
        v.f[2] = static_cast<f32>(packed & 0xFFu);         // B
    } else if (tag == "CoordinateFrame") {
        v.type = Type::CFrame;
        read_cframe(c.t, n, ok, v.f);
    } else if (tag == "OptionalCoordinateFrame") {
        v.type = Type::OptionalCFrame;
        const XNode* inner = nullptr;
        for (u32 k : n.children) {
            const std::string& ct = c.t.nodes[k].tag;
            if (ct == "CFrame" || ct == "CoordinateFrame") inner = &c.t.nodes[k];
        }
        v.present = inner != nullptr;
        if (inner) read_cframe(c.t, *inner, ok, v.f);
    } else if (tag == "Ray") {
        v.type = Type::Ray;
        if (const XNode* o = child(c.t, n, "origin")) read_children(c.t, *o, {"X", "Y", "Z"}, ok, v.f);
        if (const XNode* d = child(c.t, n, "direction")) read_children(c.t, *d, {"X", "Y", "Z"}, ok, v.f + 3);
    } else if (tag == "Faces" || tag == "Axes") {
        i64 x = 0;
        if (!parse_i64(n.text, x)) return why = "malformed bitfield", false;
        v.type = tag == "Faces" ? Type::Faces : Type::Axes;
        v.i = x;
    } else if (tag == "Rect2D") {
        v.type = Type::Rect;
        if (const XNode* mn = child(c.t, n, "min")) read_children(c.t, *mn, {"X", "Y"}, ok, v.f);
        if (const XNode* mx = child(c.t, n, "max")) read_children(c.t, *mx, {"X", "Y"}, ok, v.f + 2);
    } else if (tag == "NumberRange") {
        std::vector<f32> nums;
        if (!parse_number_list(n.text, nums) || nums.size() != 2) return why = "malformed NumberRange", false;
        v.type = Type::NumberRange;
        v.f[0] = nums[0];
        v.f[1] = nums[1];
    } else if (tag == "NumberSequence") {
        v.type = Type::NumberSequence;
        if (!parse_number_list(n.text, v.seq) || v.seq.size() % 3 != 0) return why = "malformed NumberSequence", false;
    } else if (tag == "ColorSequence") {
        v.type = Type::ColorSequence;
        if (!parse_number_list(n.text, v.seq) || v.seq.size() % 5 != 0) return why = "malformed ColorSequence", false;
    } else if (tag == "PhysicalProperties") {
        v.type = Type::PhysicalProperties;
        const XNode* custom = child(c.t, n, "CustomPhysics");
        v.present = custom && trim(custom->text) == "true";
        if (v.present) {
            static const char* const kPhys[6] = {"Density",        "Friction",         "Elasticity",
                                                 "FrictionWeight", "ElasticityWeight", "AcousticAbsorption"};
            for (usize k = 0; k < 6; ++k) {
                const XNode* e = child(c.t, n, kPhys[k]);
                if (!e) {
                    v.f[k] = k == 5 ? 1.0f : 0.0f; // AcousticAbsorption defaults to 1
                    continue;
                }
                f64 d = 0.0;
                if (!parse_f64(e->text, d)) return why = "malformed PhysicalProperties", false;
                v.f[k] = static_cast<f32>(d);
            }
        }
    } else if (tag == "Font") {
        v.type = Type::Font;
        if (const XNode* fam = child(c.t, n, "Family"))
            if (const XNode* url = child_uri(c.t, *fam)) v.s = std::string(trim(url->text));
        i64 weight = 400;
        if (const XNode* w = child(c.t, n, "Weight"))
            if (!parse_i64(w->text, weight)) return why = "malformed Font weight", false;
        v.i = weight;
        const XNode* style = child(c.t, n, "Style");
        v.aux = style && trim(style->text) == "Italic" ? 1 : 0;
    } else if (tag == "Content" || tag == "ContentId") {
        v.type = Type::Content;
        v.aux = 0;
        // One child: <null/>, <url>, or <Ref> (object content).
        for (u32 k : n.children) {
            const XNode& e = c.t.nodes[k];
            if (e.tag == "uri" || e.tag == "url") { // the format notes say uri; ContentId (legacy) uses url
                v.aux = 1;
                v.s = std::string(trim(e.text));
            } else if (e.tag == "Ref") {
                v.aux = 2;
                v.i = lookup(c.refs, e.text);
            }
            break;
        }
    } else if (tag == "UniqueId") {
        v.type = Type::UniqueId;
        v.s = std::string(trim(n.text));
        for (char& ch : v.s)
            if (ch >= 'A' && ch <= 'F') ch = static_cast<char>(ch - 'A' + 'a');
    } else if (tag == "SharedString" || tag == "NetAssetRef") {
        v.type = Type::SharedString;
        v.i = lookup(c.shared, n.text);
    } else if (tag == "Ref") {
        v.type = Type::Ref;
        v.i = lookup(c.refs, n.text);
    } else {
        return why = "unsupported value type <" + tag + ">", false;
    }
    if (!ok) return why = "malformed <" + tag + ">", false;
    return true;
}

bool is_service_name(std::string_view name) {
    static const char* const kServices[] = {
        "Workspace",     "Lighting",        "ReplicatedStorage",   "ServerScriptService",
        "ServerStorage", "StarterPlayer",   "StarterGui",          "StarterPack",
        "Players",       "SoundService",    "ReplicatedFirst",     "Chat",
        "Teams",         "TextChatService", "LocalizationService",
    };
    for (const char* s : kServices)
        if (name == s) return true;
    return false;
}

} // namespace

bool parse_xml(const u8* data, usize size, Document& out, std::string* error) {
    out = Document{};
    auto fail = [&](const std::string& msg) {
        if (error) *error = "XML: " + msg;
        return false;
    };
    if (!data && size) return fail("null input");
    Tree t;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    if (!t.parse(text)) return fail(t.error);
    const XNode& root = t.nodes[0];
    if (root.tag != "roblox") return fail("the root element is not <roblox>");
    const std::string_view version = attr(root, "version");
    if (version != "4") detail::warn(out, "XML format version '" + std::string(version) + "' (expected 4)");

    std::unordered_map<std::string, u32> shared_index;
    std::vector<const XNode*> items;
    for (u32 k : root.children) {
        const XNode& n = t.nodes[k];
        if (n.tag == "Meta") {
            out.meta.emplace_back(std::string(attr(n, "name")), n.text);
        } else if (n.tag == "SharedStrings") {
            for (u32 s : n.children) {
                const XNode& ss = t.nodes[s];
                if (ss.tag != "SharedString") continue;
                std::string bytes;
                if (!base64_decode(ss.text, bytes)) {
                    detail::warn(out, "SharedString with invalid base64 kept empty");
                    bytes.clear();
                }
                shared_index.emplace(std::string(attr(ss, "md5")), static_cast<u32>(out.shared_strings.size()));
                out.shared_strings.push_back(std::move(bytes));
            }
        } else if (n.tag == "Item") {
            items.push_back(&n);
        } // External and unknown root children are ignored
    }

    // Pass 1: create every instance (DFS order, siblings in file order) and remember the referents.
    struct Pending {
        const XNode* node;
        i32 parent;
    };
    std::vector<Pending> stack;
    for (auto it = items.rbegin(); it != items.rend(); ++it) stack.push_back({*it, -1});
    std::unordered_map<std::string, u32> refs;
    std::vector<std::pair<u32, const XNode*>> props_todo;
    std::vector<u32> order;
    while (!stack.empty()) {
        const Pending p = stack.back();
        stack.pop_back();
        const XNode& n = *p.node;
        const std::string_view cls = attr(n, "class");
        if (cls.empty()) {
            detail::warn(out, "Item without a class skipped");
            continue;
        }
        if (out.instances.size() >= kMaxInstances) return fail("too many instances");
        InstanceData d;
        d.class_name = std::string(cls);
        d.service = is_service_name(cls);
        d.parent = p.parent;
        const u32 idx = static_cast<u32>(out.instances.size());
        const std::string_view referent = attr(n, "referent");
        if (!referent.empty() && referent != "null") {
            if (refs.count(std::string(referent)))
                detail::warn(out, "duplicated referent " + std::string(referent) + " ignored");
            else
                refs.emplace(std::string(referent), idx);
        }
        out.instances.push_back(std::move(d));
        order.push_back(idx);

        std::vector<const XNode*> nested;
        for (u32 k : n.children) {
            const XNode& c = t.nodes[k];
            if (c.tag == "Properties")
                props_todo.emplace_back(idx, &c);
            else if (c.tag == "Item")
                nested.push_back(&c);
        }
        for (auto it = nested.rbegin(); it != nested.rend(); ++it) stack.push_back({*it, static_cast<i32>(idx)});
    }

    // Pass 2: property values (Ref and SharedString need the complete tables).
    const Ctx ctx{t, refs, shared_index};
    for (const auto& [idx, props] : props_todo) {
        for (u32 k : props->children) {
            const XNode& pe = t.nodes[k];
            const std::string_view name = attr(pe, "name");
            if (name.empty()) continue;
            Value v;
            std::string why;
            if (!convert(ctx, pe, name, v, why)) {
                detail::warn(out, out.instances[idx].class_name + "." + std::string(name) + ": " + why + "; skipped");
                continue;
            }
            out.instances[idx].props.push_back({std::string(name), std::move(v)});
        }
    }
    detail::finalize_tree(out, order);
    return true;
}

} // namespace tefra::rbx
