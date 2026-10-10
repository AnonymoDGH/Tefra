// Format-independent parts of the decoded document: type names, lookups, format detection, base64, the table of
// axis-aligned CFrame rotations and the tree builder shared by both readers.
#include "internal.h"

#include <cstring>

namespace tefra::rbx {

const char* type_name(Type t) {
    switch (t) {
    case Type::None: return "None";
    case Type::String: return "String";
    case Type::Bool: return "Bool";
    case Type::Int32: return "Int32";
    case Type::Float32: return "Float32";
    case Type::Float64: return "Float64";
    case Type::UDim: return "UDim";
    case Type::UDim2: return "UDim2";
    case Type::Ray: return "Ray";
    case Type::Faces: return "Faces";
    case Type::Axes: return "Axes";
    case Type::BrickColor: return "BrickColor";
    case Type::Color3: return "Color3";
    case Type::Vector2: return "Vector2";
    case Type::Vector3: return "Vector3";
    case Type::CFrame: return "CFrame";
    case Type::Enum: return "Enum";
    case Type::Ref: return "Ref";
    case Type::Vector3int16: return "Vector3int16";
    case Type::NumberSequence: return "NumberSequence";
    case Type::ColorSequence: return "ColorSequence";
    case Type::NumberRange: return "NumberRange";
    case Type::Rect: return "Rect";
    case Type::PhysicalProperties: return "PhysicalProperties";
    case Type::Color3uint8: return "Color3uint8";
    case Type::Int64: return "Int64";
    case Type::SharedString: return "SharedString";
    case Type::Bytecode: return "Bytecode";
    case Type::OptionalCFrame: return "OptionalCFrame";
    case Type::UniqueId: return "UniqueId";
    case Type::Font: return "Font";
    case Type::Content: return "Content";
    case Type::BinaryString: return "BinaryString";
    case Type::ProtectedString: return "ProtectedString";
    }
    return "?";
}

const Value* InstanceData::find(std::string_view n) const {
    for (const Property& p : props)
        if (detail::ieq(p.name, n)) return &p.value;
    return nullptr;
}

const std::string& InstanceData::name() const {
    for (const Property& p : props)
        if (p.name == "Name" && p.value.type == Type::String && !p.value.s.empty()) return p.value.s;
    return class_name;
}

Format detect_format(const u8* data, usize size) {
    if (!data) return Format::Unknown;
    static const u8 kBinaryMagic[14] = {'<', 'r', 'o', 'b', 'l', 'o', 'x', '!', 0x89, 0xff, 0x0d, 0x0a, 0x1a, 0x0a};
    if (size >= sizeof(kBinaryMagic) && std::memcmp(data, kBinaryMagic, sizeof(kBinaryMagic)) == 0)
        return Format::Binary;
    usize p = 0;
    if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) p = 3; // UTF-8 BOM
    while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' || data[p] == '\n')) ++p;
    const std::string_view rest(reinterpret_cast<const char*>(data) + p, size - p);
    if (rest.substr(0, 8) == "<roblox!") return Format::Unknown; // binary magic with a damaged signature
    if (rest.substr(0, 7) == "<roblox" || rest.substr(0, 5) == "<?xml" || rest.substr(0, 4) == "<!--")
        return Format::Xml;
    return Format::Unknown;
}

bool parse_document(const u8* data, usize size, Document& out, std::string* error) {
    switch (detect_format(data, size)) {
    case Format::Binary: return parse_binary(data, size, out, error);
    case Format::Xml: return parse_xml(data, size, out, error);
    case Format::Unknown: break;
    }
    if (error) *error = "not a Roblox place or model file (.rbxl, .rbxlx, .rbxm, .rbxmx)";
    return false;
}

// ---- base64 -----------------------------------------------------------------------------------------------------

namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64_digit(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62; // also accept the URL-safe alphabet
    if (c == '/' || c == '_') return 63;
    return -1;
}
} // namespace

std::string base64_encode(const u8* data, usize size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    usize i = 0;
    for (; i + 3 <= size; i += 3) {
        const u32 v = (u32(data[i]) << 16) | (u32(data[i + 1]) << 8) | u32(data[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i < size) {
        u32 v = u32(data[i]) << 16;
        if (i + 1 < size) v |= u32(data[i + 1]) << 8;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < size ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool base64_decode(std::string_view text, std::string& out) {
    out.clear();
    out.reserve(text.size() / 4 * 3);
    u32 acc = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        const int d = b64_digit(c);
        if (d < 0) return false;
        acc = (acc << 6) | static_cast<u32>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xffu);
        }
    }
    return true;
}

// ---- Axis-aligned CFrame rotations ---------------------------------------------------------------------------

namespace {
struct RotId {
    u8 id;
    i16 x, y, z; // degrees, applied Y, then X, then Z (R = Ry * Rx * Rz)
};
constexpr RotId kRotIds[] = {
    {0x02, 0, 0, 0},    {0x03, 90, 0, 0},   {0x05, 0, 180, 180}, {0x06, -90, 0, 0},    {0x07, 0, 180, 90},
    {0x09, 0, 90, 90},  {0x0a, 0, 0, 90},   {0x0c, 0, -90, 90},  {0x0d, -90, -90, 0},  {0x0e, 0, -90, 0},
    {0x10, 90, -90, 0}, {0x11, 0, 90, 180}, {0x14, 0, 180, 0},   {0x15, -90, -180, 0}, {0x17, 0, 0, 180},
    {0x18, 90, 180, 0}, {0x19, 0, 0, -90},  {0x1b, 0, -90, -90}, {0x1c, 0, -180, -90}, {0x1e, 0, 90, -90},
    {0x1f, 90, 90, 0},  {0x20, 0, 90, 0},   {0x22, -90, 90, 0},  {0x23, 0, -90, 180},
};

// Exact cosine / sine of a multiple of 90 degrees.
void cos_sin_90(int deg, int& c, int& s) {
    const int q = ((deg / 90) % 4 + 4) % 4;
    static const int kC[4] = {1, 0, -1, 0};
    static const int kS[4] = {0, 1, 0, -1};
    c = kC[q];
    s = kS[q];
}

void mul3(const int a[9], const int b[9], int out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            out[r * 3 + col] = a[r * 3] * b[col] + a[r * 3 + 1] * b[3 + col] + a[r * 3 + 2] * b[6 + col];
}
} // namespace

bool cframe_rotation_from_id(u8 id, f32 out[9]) {
    for (const RotId& r : kRotIds) {
        if (r.id != id) continue;
        int cx, sx, cy, sy, cz, sz;
        cos_sin_90(r.x, cx, sx);
        cos_sin_90(r.y, cy, sy);
        cos_sin_90(r.z, cz, sz);
        const int ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
        const int rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
        const int rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
        int t[9], m[9];
        mul3(ry, rx, t);
        mul3(t, rz, m);
        for (int k = 0; k < 9; ++k) out[k] = static_cast<f32>(m[k]);
        return true;
    }
    return false;
}

// ---- Shared reader helpers -------------------------------------------------------------------------------------

namespace detail {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

void warn(Document& doc, std::string message) {
    if (doc.warnings.size() < 256) doc.warnings.push_back(std::move(message));
}

void finalize_tree(Document& doc, const std::vector<u32>& order) {
    const usize n = doc.instances.size();
    for (usize i = 0; i < n; ++i) {
        InstanceData& d = doc.instances[i];
        d.children.clear();
        d.dropped = false;
        if (d.parent < -1 || d.parent >= static_cast<i64>(n) || d.parent == static_cast<i64>(i)) d.parent = -1;
    }
    doc.roots.clear();

    // Depth of every instance; cycles are cut where they are found.
    constexpr i32 kUnset = -2;
    std::vector<i32> depth(n, kUnset);
    std::vector<u8> on_path(n, 0);
    std::vector<u32> path;
    u32 cycles = 0, too_deep = 0;
    for (usize i = 0; i < n; ++i) {
        if (depth[i] != kUnset) continue;
        path.clear();
        i64 cur = static_cast<i64>(i);
        while (cur >= 0 && depth[static_cast<usize>(cur)] == kUnset && !on_path[static_cast<usize>(cur)]) {
            on_path[static_cast<usize>(cur)] = 1;
            path.push_back(static_cast<u32>(cur));
            cur = doc.instances[static_cast<usize>(cur)].parent;
        }
        i32 base = -1;
        if (cur >= 0 && depth[static_cast<usize>(cur)] != kUnset) {
            base = depth[static_cast<usize>(cur)];
        } else if (cur >= 0) { // reached a node of the current path: a cycle
            doc.instances[path.back()].parent = -1;
            ++cycles;
        }
        for (usize k = path.size(); k-- > 0;) {
            base = base <= static_cast<i32>(kMaxDepth) ? base + 1 : base; // saturates: dropped anyway
            depth[path[k]] = base;
            on_path[path[k]] = 0;
        }
    }
    for (usize i = 0; i < n; ++i)
        if (depth[i] > static_cast<i32>(kMaxDepth)) {
            doc.instances[i].dropped = true;
            ++too_deep;
        }

    std::vector<u8> placed(n, 0);
    auto place = [&](u32 idx) {
        if (idx >= n || placed[idx]) return;
        placed[idx] = 1;
        InstanceData& d = doc.instances[idx];
        if (d.dropped) return;
        if (d.parent < 0)
            doc.roots.push_back(idx);
        else
            doc.instances[static_cast<usize>(d.parent)].children.push_back(idx);
    };
    for (u32 idx : order) place(idx);
    for (usize i = 0; i < n; ++i) place(static_cast<u32>(i));

    if (cycles) warn(doc, "parent cycles cut: " + std::to_string(cycles));
    if (too_deep)
        warn(doc, std::to_string(too_deep) + " instances nested deeper than " + std::to_string(kMaxDepth) +
                      " levels were dropped");
}

} // namespace detail

} // namespace tefra::rbx
