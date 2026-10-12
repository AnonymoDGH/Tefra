// Roblox place / model importer (ADR-017): turns a decoded rbx::Document into the Tefra DataModel. Clean-room: the
// conversion rules come from the Tefra specification and the public format description (docs/research/m3b-rbx).
#include "tefra/rbx/importer.h"

#include "internal.h"
#include "tefra/core/fs.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tefra::rbx {

using world::DataModel;
using world::Instance;

namespace {

using world::ClassRegistry;
using WValue = world::Value;

constexpr usize kMaxBlobChars = 4096; // binary blobs in OriginalProperties are cut to this many base64 chars
constexpr f64 kRobloxGravity = 196.2; // Roblox default gravity in studs/s^2 (used for the jump-power conversion)

// ---- Constants of the Roblox enums (public documentation; see docs/research/m3b-rbx/REPORT.md) -----------------

// Partial BrickColor palette (number -> sRGB). Numbers outside this table fall back to kDefaultBrickColor (warning).
struct BrickEntry {
    u32 number;
    u8 r, g, b;
};
constexpr BrickEntry kBrickColors[] = {
    {1, 242, 243, 242},  {2, 161, 165, 162},   {5, 215, 197, 153},   {9, 232, 186, 200},  {11, 128, 187, 219},
    {18, 204, 142, 105}, {21, 196, 40, 28},    {23, 13, 105, 172},   {24, 245, 205, 47},  {26, 27, 42, 53},
    {28, 40, 127, 70},   {37, 75, 151, 75},    {38, 160, 95, 52},    {39, 193, 202, 222}, {41, 205, 84, 75},
    {45, 180, 210, 228}, {101, 218, 134, 122}, {102, 110, 153, 202}, {104, 107, 50, 124}, {105, 226, 155, 64},
    {107, 0, 143, 156},  {119, 164, 189, 71},  {125, 234, 184, 146}, {192, 105, 64, 40},  {194, 163, 162, 165},
    {199, 99, 95, 98},   {208, 229, 228, 225}, {226, 253, 234, 141},
};

// Roblox Material token -> Tefra Material name (nearest equivalent). nullptr = unknown.
const char* material_name(i64 token) {
    switch (token) {
    case 256: return "Plastic";
    case 272: return "SmoothPlastic";
    case 288: return "Neon";
    case 512: return "Wood";
    case 528: return "WoodPlanks";
    case 784: return "Marble";
    case 800: return "Slate";
    case 816: return "Concrete";
    case 832: return "Granite";
    case 848: return "Brick";
    case 864: return "Ground"; // Pebble
    case 880: return "Cobblestone";
    case 896: return "Rock";
    case 912: return "Sand";   // Sandstone
    case 1040: return "Metal"; // CorrodedMetal
    case 1056: return "DiamondPlate";
    case 1072: return "Foil";
    case 1088: return "Metal";
    case 1280: return "Grass";
    case 1284: return "Grass"; // LeafyGrass
    case 1296: return "Sand";
    case 1312: return "Fabric";
    case 1328: return "Marble"; // Snow
    case 1344: return "Ground"; // Mud
    case 1360: return "Ground";
    case 1376: return "Concrete"; // Asphalt
    case 1392: return "Marble";   // Salt
    case 1536: return "Ice";
    case 1552: return "Ice"; // Glacier
    case 1568: return "Glass";
    case 1584: return "Neon";  // ForceField
    case 1792: return "Glass"; // Air (no visible surface)
    case 2048: return "Glass"; // Water
    default: return nullptr;
    }
}

struct Mat3 {
    f64 m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // row-major; column 0 = right, 1 = up, 2 = back (Roblox)
};

Mat3 mat_mul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            f64 s = 0.0;
            for (int k = 0; k < 3; ++k) s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

// The rotation of a CFrame's f[3..11] (R00 R01 R02 R10 ... R22).
Mat3 mat_from_cframe(const f32* f) {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = static_cast<f64>(f[3 + 3 * i + j]);
    return r;
}

// Roblox cylinders run along X, Tefra's along Y: the local frame turns by Rz(-90 deg) (Tefra Y = Roblox X).
Mat3 cylinder_axis_fix() {
    Mat3 r;
    r.m[0][0] = 0;
    r.m[0][1] = 1;
    r.m[0][2] = 0;
    r.m[1][0] = -1;
    r.m[1][1] = 0;
    r.m[1][2] = 0;
    r.m[2][0] = 0;
    r.m[2][1] = 0;
    r.m[2][2] = 1;
    return r;
}

// Standard rotation-matrix -> quaternion conversion (Shepperd's method, numerically stable branches).
quat quat_from_matrix(const Mat3& r) {
    const f64(&m)[3][3] = r.m;
    const f64 tr = m[0][0] + m[1][1] + m[2][2];
    f64 x = 0, y = 0, z = 0, w = 1;
    if (tr > 0.0) {
        const f64 s = std::sqrt(tr + 1.0) * 2.0;
        w = 0.25 * s;
        x = (m[2][1] - m[1][2]) / s;
        y = (m[0][2] - m[2][0]) / s;
        z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const f64 s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        w = (m[2][1] - m[1][2]) / s;
        x = 0.25 * s;
        y = (m[0][1] + m[1][0]) / s;
        z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const f64 s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        w = (m[0][2] - m[2][0]) / s;
        x = (m[0][1] + m[1][0]) / s;
        y = 0.25 * s;
        z = (m[1][2] + m[2][1]) / s;
    } else {
        const f64 s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        w = (m[1][0] - m[0][1]) / s;
        x = (m[0][2] + m[2][0]) / s;
        y = (m[1][2] + m[2][1]) / s;
        z = 0.25 * s;
    }
    return normalize(quat{static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z), static_cast<f32>(w)});
}

vec3 orientation_of(const Mat3& r) { return world::quat_to_orientation(quat_from_matrix(r)); }

u32 enum_index(const char* enum_name, const char* item) {
    const world::EnumInfo* e = ClassRegistry::get().find_enum(enum_name);
    const i32 i = e ? e->index_of(item) : -1;
    return i < 0 ? 0u : static_cast<u32>(i);
}

bool ascii_alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Reserved C# words and the engine names a script class must not shadow.
bool csharp_reserved(const std::string& w) {
    static const char* const kWords[] = {
        "abstract", "as",      "base",    "bool",       "break",    "byte",     "case",        "catch",     "char",
        "checked",  "class",   "const",   "continue",   "decimal",  "default",  "delegate",    "do",        "double",
        "else",     "enum",    "event",   "explicit",   "extern",   "false",    "finally",     "fixed",     "float",
        "for",      "foreach", "goto",    "if",         "implicit", "in",       "int",         "interface", "internal",
        "is",       "lock",    "long",    "namespace",  "new",      "null",     "object",      "operator",  "out",
        "override", "params",  "private", "protected",  "public",   "readonly", "ref",         "return",    "sbyte",
        "sealed",   "short",   "sizeof",  "stackalloc", "static",   "string",   "struct",      "switch",    "this",
        "throw",    "true",    "try",     "typeof",     "uint",     "ulong",    "unchecked",   "unsafe",    "ushort",
        "using",    "virtual", "void",    "volatile",   "while",    "Tefra",    "TefraScript", "System"};
    for (const char* k : kWords)
        if (w == k) return true;
    return false;
}

// Identifier base of the C# class of a script: letters, digits and '_', no leading digit, not reserved.
std::string csharp_base(const std::string& name) {
    std::string base;
    for (char c : name) base += ascii_alnum(c) ? c : '_';
    if (base.empty() || (base[0] >= '0' && base[0] <= '9')) base = "Script_" + base;
    if (csharp_reserved(base)) base += "_";
    return base;
}

// Makes text safe inside a C# line comment: C# ends a comment at CR, LF, U+0085, U+2028 and U+2029, so those (and
// NUL bytes) become spaces.
std::string scrub_line(std::string s) {
    for (usize i = 0; i < s.size(); ++i) {
        const u8 c = static_cast<u8>(s[i]);
        if (c == '\r' || c == '\n' || c == 0) {
            s[i] = ' ';
        } else if (c == 0xC2 && i + 1 < s.size() && static_cast<u8>(s[i + 1]) == 0x85) {
            s[i] = ' ';
            s[i + 1] = ' ';
            ++i;
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<u8>(s[i + 1]) == 0x80 &&
                   (static_cast<u8>(s[i + 2]) == 0xA8 || static_cast<u8>(s[i + 2]) == 0xA9)) {
            s[i] = ' ';
            s[i + 1] = ' ';
            s[i + 2] = ' ';
            i += 2;
        }
    }
    return s;
}

// The C# stub that replaces a Roblox script: the Luau original as a block of line comments, then an empty class.
// ModuleScripts are compiled (never run), so the stub must compile on its own.
std::string script_skeleton(const std::string& cls, const std::string& name, const std::string& luau,
                            const std::string& class_name) {
    std::string out;
    out += "// Imported from a Roblox " + cls + " \"" + scrub_line(name) + "\".\n";
    out += "// The Luau original is kept below as comments (and in LuauSource). Rewrite the logic in C#.\n";
    out += "// The script is imported disabled (Enabled = false) so nothing runs until it is translated.\n";
    out += "// Luau original:\n";
    usize start = 0;
    for (;;) {
        usize end = luau.find('\n', start);
        if (end == std::string::npos) end = luau.size();
        std::string line = luau.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out += "// " + scrub_line(line) + "\n";
        if (end >= luau.size()) break;
        start = end + 1;
    }
    out += "\nusing Tefra;\n\n";
    out += "public class " + class_name + " : TefraScript\n{\n";
    out += "    public override void Start() { }\n";
    out += "}\n";
    return out;
}

// ---- Property readers (by serialized name, case-insensitive, any compatible type) ------------------------------

std::optional<f64> num_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v) return std::nullopt;
    switch (v->type) {
    case Type::Float32:
    case Type::Float64: return v->d;
    case Type::Int32:
    case Type::Int64: return static_cast<f64>(v->i);
    default: return std::nullopt;
    }
}

std::optional<i64> int_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v) return std::nullopt;
    switch (v->type) {
    case Type::Int32:
    case Type::Int64:
    case Type::Enum:
    case Type::BrickColor: return v->i;
    default: return std::nullopt;
    }
}

std::optional<bool> bool_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v || v->type != Type::Bool) return std::nullopt;
    return v->i != 0;
}

// Strings: String / ProtectedString / BinaryString (raw bytes) and Content URIs.
std::optional<std::string> text_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v) return std::nullopt;
    switch (v->type) {
    case Type::String:
    case Type::ProtectedString:
    case Type::BinaryString: return v->s;
    case Type::Content: return v->aux == 1 ? std::optional<std::string>(v->s) : std::nullopt;
    default: return std::nullopt;
    }
}

std::optional<vec3> vec3_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v || (v->type != Type::Vector3 && v->type != Type::Vector3int16)) return std::nullopt;
    return vec3{v->f[0], v->f[1], v->f[2]};
}

// Colour from Color3uint8 (0-255) or Color3 (0-1).
std::optional<vec3> color_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v) return std::nullopt;
    if (v->type == Type::Color3uint8) return vec3{v->f[0] / 255.0f, v->f[1] / 255.0f, v->f[2] / 255.0f};
    if (v->type == Type::Color3) return vec3{v->f[0], v->f[1], v->f[2]};
    return std::nullopt;
}

// Roblox BrickColor number -> sRGB (nullopt when the number is not in the partial palette).
std::optional<vec3> brick_rgb(i64 number) {
    for (const BrickEntry& e : kBrickColors)
        if (static_cast<i64>(e.number) == number) return vec3{e.r / 255.0f, e.g / 255.0f, e.b / 255.0f};
    return std::nullopt;
}

// Part colour: Color3uint8, then Color3, then the BrickColor number.
std::optional<vec3> part_color(const InstanceData& d, bool& unknown_brick) {
    if (auto c = color_of(d, "Color3uint8")) return c;
    if (auto c = color_of(d, "Color")) return c;
    if (auto n = int_of(d, "BrickColor")) {
        if (auto rgb = brick_rgb(*n)) return rgb;
        unknown_brick = true;
        return vec3{163 / 255.0f, 162 / 255.0f, 165 / 255.0f}; // BrickColor kDefaultBrickColor
    }
    return std::nullopt;
}

// UDim2 -> {X.scale, X.offset, Y.scale, Y.offset}.
std::optional<std::array<f32, 4>> udim2_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v || v->type != Type::UDim2) return std::nullopt;
    return std::array<f32, 4>{v->f[0], v->f[1], v->f[2], v->f[3]};
}

std::optional<vec3> vec2_of(const InstanceData& d, std::string_view name) {
    const Value* v = d.find(name);
    if (!v || v->type != Type::Vector2) return std::nullopt;
    return vec3{v->f[0], v->f[1], 0.0f};
}

// CFrame: position in studs, rotation matrix.
bool cframe_of(const InstanceData& d, std::string_view name, const Value*& out) {
    const Value* v = d.find(name);
    if (!v || v->type != Type::CFrame) return false;
    out = v;
    return true;
}

// ---- Conversion ------------------------------------------------------------------------------------------------

bool is_part_class(std::string_view c) {
    return c == "Part" || c == "WedgePart" || c == "CornerWedgePart" || c == "SpawnLocation" || c == "Seat" ||
           c == "VehicleSeat" || c == "TrussPart" || c == "UnionOperation" || c == "NegateOperation";
}

bool is_script_class(std::string_view c) { return c == "Script" || c == "LocalScript" || c == "ModuleScript"; }

bool is_gui_class(std::string_view c) {
    return c == "Frame" || c == "TextLabel" || c == "TextButton" || c == "ImageLabel" || c == "ImageButton";
}

bool is_button(std::string_view c) { return c == "TextButton" || c == "ImageButton"; }

bool is_same_name_class(std::string_view c) {
    return c == "Folder" || c == "Model" || c == "Animator" || c == "Animation" || c == "Humanoid" || c == "Motor6D" ||
           c == "Camera" || c == "PointLight" || c == "SpotLight" || c == "Sound" || c == "ScreenGui" ||
           c == "StarterPlayerScripts" || c == "StarterCharacterScripts";
}

// Roblox NormalId token -> Tefra NormalId name. Roblox orders the faces Right, Top, Back, Left, Bottom, Front (the
// order of the Faces bit mask); unknown tokens become Front.
const char* face_name(i64 token) {
    static const char* const kFaces[] = {"Right", "Top", "Back", "Left", "Bottom", "Front"};
    return token >= 0 && token < 6 ? kFaces[token] : "Front";
}

// Roblox ParticleEmitter: ranges become a mean plus Variation, sequences their first and last keys. Textures are
// Roblox asset ids, so the look is picked from the emitter's name (fire, smoke, spark, star, snow) or a soft circle.
void apply_particles(const InstanceData& d, Instance& inst, f64 scale) {
    auto range = [&](const char* prop, const char* dst, f64 mul) -> f64 {
        const Value* v = d.find(prop);
        if (!v || v->type != Type::NumberRange) return 0.0;
        const f64 lo = v->f[0], hi = v->f[1], mid = 0.5 * (lo + hi);
        inst.set(dst, WValue::number(mid * mul));
        return mid > 1e-6 ? (hi - lo) * 0.5 / mid : 0.0;
    };
    auto seq = [&](const char* prop, const char* first, const char* last, f64 mul) {
        const Value* v = d.find(prop);
        if (!v || v->type != Type::NumberSequence || v->seq.size() < 3) return;
        inst.set(first, WValue::number(v->seq[1] * mul));
        inst.set(last, WValue::number(v->seq[v->seq.size() - 2] * mul));
    };
    f64 var = 0.0;
    var = max(var, range("Lifetime", "Lifetime", 1.0));
    var = max(var, range("Speed", "Speed", scale));
    inst.set("Variation", WValue::number(min(var, 1.0)));
    range("RotSpeed", "RotSpeed", 1.0);
    seq("Size", "Size", "SizeEnd", scale);
    seq("Transparency", "Transparency", "TransparencyEnd", 1.0);
    if (const Value* c = d.find("Color"); c && c->type == Type::ColorSequence && c->seq.size() >= 5) {
        const usize n = c->seq.size();
        inst.set("Color", WValue::color3({c->seq[1], c->seq[2], c->seq[3]}));
        inst.set("ColorEnd", WValue::color3({c->seq[n - 4], c->seq[n - 3], c->seq[n - 2]}));
    }
    if (const Value* sp = d.find("SpreadAngle")) inst.set("SpreadAngle", WValue::number(max(sp->f[0], sp->f[1])));
    if (auto v = num_of(d, "Rate")) inst.set("Rate", WValue::number(*v));
    if (auto v = num_of(d, "LightEmission")) inst.set("LightEmission", WValue::number(*v));
    if (auto v = num_of(d, "Brightness")) inst.set("Brightness", WValue::number(*v));
    if (auto v = num_of(d, "Drag")) inst.set("Drag", WValue::number(*v));
    if (auto v = num_of(d, "TimeScale")) inst.set("TimeScale", WValue::number(*v));
    if (auto v = bool_of(d, "Enabled")) inst.set("Enabled", WValue::boolean(*v));
    if (auto v = bool_of(d, "LockedToPart")) inst.set("LockedToPart", WValue::boolean(*v));
    if (auto v = vec3_of(d, "Acceleration")) inst.set("Acceleration", WValue::vector3(*v * f32(scale)));
    if (auto v = int_of(d, "EmissionDirection"))
        inst.set("EmissionDirection", WValue::enumeration(enum_index("NormalId", face_name(*v))));
    std::string lower = d.name();
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const char* look = "builtin:circle";
    if (lower.find("fire") != std::string::npos || lower.find("flame") != std::string::npos)
        look = "builtin:fire";
    else if (lower.find("smoke") != std::string::npos || lower.find("dust") != std::string::npos)
        look = "builtin:smoke";
    else if (lower.find("spark") != std::string::npos)
        look = "builtin:spark";
    else if (lower.find("star") != std::string::npos || lower.find("magic") != std::string::npos)
        look = "builtin:star";
    else if (lower.find("snow") != std::string::npos)
        look = "builtin:snow";
    inst.set("Texture", WValue::string(look));
}

JsonValue num_json(f64 x) {
    if (std::isnan(x)) return JsonValue("NAN");
    if (std::isinf(x)) return JsonValue(x > 0 ? "INF" : "-INF");
    return JsonValue(x);
}

JsonValue floats_json(const f32* f, usize n) {
    JsonValue a = JsonValue::array();
    for (usize i = 0; i < n; ++i) a.push(num_json(static_cast<f64>(f[i])));
    return a;
}

std::string truncated_base64(const std::string& bytes) {
    // Only the bytes that can reach the cut are encoded (4 base64 chars per 3 bytes).
    const usize max_bytes = kMaxBlobChars / 4 * 3;
    const usize n = std::min(bytes.size(), max_bytes);
    std::string b64 = base64_encode(reinterpret_cast<const u8*>(bytes.data()), n);
    if (b64.size() > kMaxBlobChars) b64.resize(kMaxBlobChars);
    return b64;
}

// ---- Roblox Attributes / Tags blobs ------------------------------------------------------------------------------

constexpr usize kMaxAttributes = 4096;

u32 read_u32le(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
f32 read_f32le(const u8* p) {
    f32 f = 0.0f;
    std::memcpy(&f, p, 4);
    return f;
}
f64 read_f64le(const u8* p) {
    f64 d = 0.0;
    std::memcpy(&d, p, 8);
    return d;
}

// AttributesSerialize: u32 LE count, then per entry a u32 LE length + name, a type byte and the value. The type ids
// reuse the property table of docs/research/m3b-rbx/FORMAT.md (1.4) with plain little-endian values:
// 01 String (u32 LE length + bytes), 02 Bool, 03 Int32, 05 Float64, 07 UDim2 (kept as text), 0C Color3, 0E Vector3.
// NOT verified against a file saved by Roblox (see docs/research/rbx-import/REPORT.md): an unknown type id or a
// length that does not add up fails the whole blob, which is then kept as rbx.AttributesSerialize.
bool decode_attributes(const std::string& blob, std::vector<std::pair<std::string, WValue>>& out) {
    out.clear();
    usize pos = 0;
    auto take = [&](usize n, const u8*& p) {
        if (n > blob.size() - pos) return false;
        p = reinterpret_cast<const u8*>(blob.data()) + pos;
        pos += n;
        return true;
    };
    const u8* p = nullptr;
    if (!take(4, p)) return false;
    const u32 count = read_u32le(p);
    if (count > kMaxAttributes) return false;
    for (u32 i = 0; i < count; ++i) {
        if (!take(4, p)) return false;
        const u32 name_len = read_u32le(p);
        if (!take(name_len, p)) return false;
        std::string name(reinterpret_cast<const char*>(p), name_len);
        if (!take(1, p)) return false;
        const u8 type = p[0];
        switch (type) {
        case 0x01: {
            if (!take(4, p)) return false;
            const u32 n = read_u32le(p);
            if (!take(n, p)) return false;
            out.emplace_back(std::move(name), WValue::string(std::string(reinterpret_cast<const char*>(p), n)));
            break;
        }
        case 0x02:
            if (!take(1, p)) return false;
            out.emplace_back(std::move(name), WValue::boolean(p[0] != 0));
            break;
        case 0x03:
            if (!take(4, p)) return false;
            out.emplace_back(std::move(name), WValue::integer(static_cast<i32>(read_u32le(p))));
            break;
        case 0x05:
            if (!take(8, p)) return false;
            out.emplace_back(std::move(name), WValue::number(read_f64le(p)));
            break;
        case 0x07: {
            if (!take(16, p)) return false;
            char buf[128];
            std::snprintf(buf, sizeof buf, "UDim2(%g,%g,%g,%g)", static_cast<f64>(read_f32le(p)),
                          static_cast<f64>(read_f32le(p + 4)), static_cast<f64>(read_f32le(p + 8)),
                          static_cast<f64>(read_f32le(p + 12)));
            out.emplace_back(std::move(name), WValue::string(buf));
            break;
        }
        case 0x0C:
            if (!take(12, p)) return false;
            out.emplace_back(std::move(name),
                             WValue::color3(vec3{read_f32le(p), read_f32le(p + 4), read_f32le(p + 8)}));
            break;
        case 0x0E:
            if (!take(12, p)) return false;
            out.emplace_back(std::move(name),
                             WValue::vector3(vec3{read_f32le(p), read_f32le(p + 4), read_f32le(p + 8)}));
            break;
        default: return false;
        }
    }
    return pos == blob.size();
}

// Tags: the names separated by NUL bytes.
std::vector<std::string> split_tags(const std::string& blob) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : blob) {
        if (c == '\0') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_value_class(std::string_view c) {
    return c == "StringValue" || c == "IntValue" || c == "NumberValue" || c == "BoolValue" || c == "Vector3Value" ||
           c == "Color3Value" || c == "ObjectValue" || c == "CFrameValue";
}

// Lighting children that Tefra has no class for: their properties are kept as rbx.<Class>.<Prop> on Lighting.
bool is_lighting_effect(std::string_view c) {
    return c == "Atmosphere" || c == "Sky" || c == "Clouds" || c == "BloomEffect" || c == "ColorCorrectionEffect" ||
           c == "SunRaysEffect" || c == "DepthOfFieldEffect" || c == "BlurEffect";
}

bool is_ui_modifier(std::string_view c) {
    return c == "UIStroke" || c == "UIPadding" || c == "UIListLayout" || c == "UIGridLayout" || c == "UIGradient" ||
           c == "UIAspectRatioConstraint";
}

// GUI classes with no Tefra class of their own: TextBox has one (a TextLabel); the others become Frames.
bool is_gui_extra(std::string_view c) {
    return c == "TextBox" || c == "ScrollingFrame" || c == "CanvasGroup" || c == "ViewportFrame";
}

class Converter {
  public:
    Converter(const Document& doc, DataModel& dm, const ImportOptions& opt, ImportReport& rep)
        : doc_(doc), dm_(dm), opt_(opt), rep_(rep), s_(opt.stud_to_meters), made_(doc.instances.size(), nullptr),
          assets_dir_(opt.assets_dir) {
        for (const std::string& w : doc.warnings) warn(w);
        rep_.instances = static_cast<u32>(doc.instances.size());
        for (const InstanceData& d : doc.instances) {
            ++rep_.class_counts[d.class_name];
            if (is_script_class(d.class_name)) ++script_names_[csharp_base(d.name())];
        }
    }

    void run_place();
    void run_model(Instance& parent, std::vector<Instance*>* roots);

  private:
    struct Deferred {
        Instance* owner;
        const char* prop;
        u32 target;
    };

    void warn(const std::string& msg) {
        auto it = warn_index_.find(msg);
        if (it == warn_index_.end()) {
            warn_index_.emplace(msg, warnings_.size());
            warnings_.push_back({msg, 1});
        } else {
            ++warnings_[it->second].second;
        }
    }

    void finish();
    void defer_ref(Instance* owner, const char* prop, const Value* ref) {
        if (ref && ref->type == Type::Ref && ref->i >= 0 && static_cast<usize>(ref->i) < made_.size())
            deferred_.push_back({owner, prop, static_cast<u32>(ref->i)});
    }

    Instance* convert(u32 idx, Instance* parent);
    void convert_children(u32 idx, Instance& inst);
    Instance* placeholder(u32 idx, Instance* parent, const std::string& name);
    void mark_approximated(const InstanceData& d, Instance& inst);
    void dump_props(const InstanceData& d, Instance& inst, const std::string& prefix);
    void apply_extras(const InstanceData& d, Instance& inst);
    std::optional<WValue> attribute_value(const Value& v) const;
    std::string special_mesh_file(u32 idx) const;
    std::string asset_path(const std::string& text, std::initializer_list<const char*> exts) const;
    std::string script_class(const std::string& name, u32 index) const;
    void apply_service(const InstanceData& d, Instance& svc);
    void apply_base(const InstanceData& d, Instance& inst, bool cylinder);
    void apply_cframe(const InstanceData& d, Instance& inst, const char* prop, const char* pos, const char* ori);
    Instance* convert_part(u32 idx, Instance* parent, const std::string& name);
    Instance* convert_script(u32 idx, Instance* parent, const std::string& name);
    void apply_light(const InstanceData& d, Instance& inst);
    void apply_humanoid(const InstanceData& d, Instance& inst);
    void apply_camera(const InstanceData& d, Instance& inst);
    void apply_gui_common(const InstanceData& d, Instance& inst);
    void apply_text(const InstanceData& d, Instance& inst);
    void apply_image(const InstanceData& d, Instance& inst);
    void apply_corner(const InstanceData& d, Instance& gui);
    void apply_clouds(const InstanceData& d, Instance& lighting);
    void apply_decal(const InstanceData& d, Instance& inst);
    void apply_value(const InstanceData& d, Instance& inst);
    void apply_modifier(const InstanceData& d, Instance& inst);
    JsonValue value_json(const Value& v) const;

    const Document& doc_;
    DataModel& dm_;
    const ImportOptions& opt_;
    ImportReport& rep_;
    f64 s_;
    std::vector<Instance*> made_;
    std::string assets_dir_;
    std::vector<Deferred> deferred_;
    std::vector<std::pair<std::string, u32>> warnings_;
    std::unordered_map<std::string, usize> warn_index_;
    std::unordered_map<std::string, u32> script_names_; // csharp_base -> number of scripts with that base
};

void Converter::finish() {
    for (const Deferred& df : deferred_) {
        if (df.target >= made_.size() || !made_[df.target]) continue;
        df.owner->set(df.prop, WValue::reference(made_[df.target]->id()));
    }
    rep_.warnings.clear();
    for (const auto& [msg, n] : warnings_) rep_.warnings.push_back(n > 1 ? msg + " (x" + std::to_string(n) + ")" : msg);
}

void Converter::run_place() {
    Instance* replicated = dm_.service("ReplicatedStorage");
    for (u32 r : doc_.roots) {
        const InstanceData& d = doc_.instances[r];
        if (Instance* svc = dm_.service(d.class_name)) {
            ++rep_.converted;
            made_[r] = svc;
            apply_service(d, *svc);
            dump_props(d, *svc, "rbx.");
            apply_extras(d, *svc);
            convert_children(r, *svc);
        } else {
            // Services Tefra does not have (ServerStorage, Teams, StarterPack...): a placeholder under
            // ReplicatedStorage, unless empty.
            if (d.children.empty() || !replicated) continue;
            Instance* holder = placeholder(r, replicated, d.name());
            apply_extras(d, *holder);
            made_[r] = holder;
            convert_children(r, *holder);
        }
    }
    finish();
}

void Converter::run_model(Instance& parent, std::vector<Instance*>* roots) {
    for (u32 r : doc_.roots) {
        const InstanceData& d = doc_.instances[r];
        if (dm_.service(d.class_name)) {
            // A place file: the contents of its services are merged into the parent.
            for (u32 c : d.children) {
                Instance* i = convert(c, &parent);
                if (roots) roots->push_back(i);
            }
            continue;
        }
        Instance* i = convert(r, &parent);
        if (roots) roots->push_back(i);
    }
    finish();
}

void Converter::convert_children(u32 idx, Instance& inst) {
    for (u32 c : doc_.instances[idx].children) {
        const InstanceData& cd = doc_.instances[c];
        if (cd.class_name == "UICorner" && inst.is_a("GuiObject")) {
            apply_corner(cd, inst);
            dump_props(cd, inst, "rbx.UICorner.");
            ++rep_.converted;
            continue;
        }
        if (inst.class_name() == "Lighting" && is_lighting_effect(cd.class_name)) {
            // Tefra has no sky, fog or post-effect classes yet: their properties are kept on Lighting.
            if (cd.class_name == "Clouds") apply_clouds(cd, inst);
            dump_props(cd, inst, "rbx." + cd.class_name + ".");
            ++rep_.converted;
            for (u32 g : cd.children) convert(g, &inst); // children move up to Lighting
            continue;
        }
        if (cd.class_name == "SpecialMesh" && inst.is_a("BasePart")) {
            // Absorbed by the part (a file mesh made the part a MeshPart already): the properties are kept.
            dump_props(cd, inst, "rbx.SpecialMesh.");
            ++rep_.converted;
            for (u32 g : cd.children) convert(g, &inst);
            continue;
        }
        convert(c, &inst);
    }
}

Instance* Converter::convert(u32 idx, Instance* parent) {
    const InstanceData& d = doc_.instances[idx];
    const std::string& cls = d.class_name;
    const std::string name = d.name();
    Instance* inst = nullptr;

    if (is_same_name_class(cls)) {
        inst = dm_.create_internal(cls, parent, name);
        if (cls == "Model")
            defer_ref(inst, "PrimaryPart", d.find("PrimaryPart"));
        else if (cls == "Humanoid")
            apply_humanoid(d, *inst);
        else if (cls == "Camera")
            apply_camera(d, *inst);
        else if (cls == "ScreenGui") {
            if (auto v = bool_of(d, "Enabled")) inst->set("Enabled", WValue::boolean(*v));
            if (auto v = int_of(d, "DisplayOrder")) inst->set("DisplayOrder", WValue::integer(*v));
            if (auto v = bool_of(d, "ResetOnSpawn")) inst->set("ResetOnSpawn", WValue::boolean(*v));
        } else if (cls == "PointLight" || cls == "SpotLight") {
            apply_light(d, *inst);
        } else if (cls == "Sound") {
            if (auto v = text_of(d, "SoundId")) {
                const std::string file = asset_path(*v, {".ogg", ".mp3", ".wav", ".flac"});
                if (!file.empty()) ++rep_.assets_linked;
                inst->set("SoundId", WValue::string(file.empty() ? *v : file));
            }
            if (auto v = num_of(d, "Volume")) inst->set("Volume", WValue::number(*v));
            if (auto v = num_of(d, "PlaybackSpeed")) inst->set("PlaybackSpeed", WValue::number(*v));
            if (auto v = bool_of(d, "Looped")) inst->set("Looped", WValue::boolean(*v));
            if (auto v = bool_of(d, "Playing")) inst->set("Playing", WValue::boolean(*v));
            if (auto v = bool_of(d, "PlayOnRemove")) inst->set("PlayOnRemove", WValue::boolean(*v));
            if (auto v = num_of(d, "RollOffMinDistance")) inst->set("RollOffMinDistance", WValue::number(*v * s_));
            if (auto v = num_of(d, "RollOffMaxDistance")) inst->set("RollOffMaxDistance", WValue::number(*v * s_));
        }
    } else if (cls == "Team") {
        inst = dm_.create_internal(cls, parent, name);
        if (auto c = color_of(d, "TeamColor"))
            inst->set("TeamColor", WValue::color3(*c));
        else if (auto n = int_of(d, "TeamColor"))
            if (auto rgb = brick_rgb(*n)) inst->set("TeamColor", WValue::color3(*rgb));
        if (auto v = bool_of(d, "AutoAssignable")) inst->set("AutoAssignable", WValue::boolean(*v));
    } else if (cls == "ParticleEmitter") {
        inst = dm_.create_internal(cls, parent, name);
        apply_particles(d, *inst, s_);
    } else if (cls == "Motor6D" || cls == "Weld") {
        inst = dm_.create_internal(cls, parent, name);
        defer_ref(inst, "Part0", d.find("Part0"));
        defer_ref(inst, "Part1", d.find("Part1"));
        for (const char* side : {"C0", "C1"}) {
            const Value* cf = nullptr;
            if (!cframe_of(d, side, cf)) continue;
            const Mat3 rot = mat_from_cframe(cf->f);
            const std::string base = side;
            inst->set(base, WValue::vector3({static_cast<f32>(cf->f[0] * s_), static_cast<f32>(cf->f[1] * s_),
                                             static_cast<f32>(cf->f[2] * s_)}));
            inst->set(base + "Orientation", WValue::vector3(orientation_of(rot)));
        }
    } else if (cls == "WeldConstraint") {
        inst = dm_.create_internal(cls, parent, name);
        defer_ref(inst, "Part0", d.find("Part0"));
        defer_ref(inst, "Part1", d.find("Part1"));
    } else if (cls == "Attachment") {
        inst = dm_.create_internal(cls, parent, name);
        apply_cframe(d, *inst, "CFrame", "Position", "Orientation");
    } else if (cls == "Decal" || cls == "Texture") {
        inst = dm_.create_internal(cls, parent, name);
        apply_decal(d, *inst);
    } else if (cls == "SurfaceGui") {
        inst = dm_.create_internal(cls, parent, name);
        if (auto v = bool_of(d, "Enabled")) inst->set("Enabled", WValue::boolean(*v));
        if (auto f = int_of(d, "Face")) inst->set("Face", WValue::enumeration(enum_index("NormalId", face_name(*f))));
        if (auto v = bool_of(d, "AlwaysOnTop")) inst->set("AlwaysOnTop", WValue::boolean(*v));
        if (auto v = num_of(d, "PixelsPerStud")) inst->set("PixelsPerStud", WValue::number(*v / s_));
    } else if (cls == "BillboardGui") {
        inst = dm_.create_internal(cls, parent, name);
        if (auto v = bool_of(d, "Enabled")) inst->set("Enabled", WValue::boolean(*v));
        defer_ref(inst, "Adornee", d.find("Adornee"));
        if (auto sz = udim2_of(d, "Size")) inst->set("Size", WValue::vector3(vec3{(*sz)[1], (*sz)[3], 0.0f}));
        if (auto o = vec3_of(d, "StudsOffset")) inst->set("StudsOffset", WValue::vector3(*o * f32(s_)));
        if (auto v = bool_of(d, "AlwaysOnTop")) inst->set("AlwaysOnTop", WValue::boolean(*v));
        if (auto v = num_of(d, "MaxDistance")) inst->set("MaxDistance", WValue::number(*v * s_));
    } else if (is_value_class(cls)) {
        inst = dm_.create_internal(cls, parent, name);
        apply_value(d, *inst);
    } else if (is_part_class(cls)) {
        inst = convert_part(idx, parent, name);
    } else if (cls == "MeshPart") {
        inst = dm_.create_internal(cls, parent, name);
        apply_base(d, *inst, false);
        const std::string mesh = text_of(d, "MeshId").value_or(std::string());
        const std::string file = asset_path(mesh, {".glb", ".gltf"});
        if (!file.empty()) {
            inst->set("MeshId", WValue::string(file));
            ++rep_.assets_linked;
        } else {
            inst->set("MeshId", WValue::string(mesh));
            mark_approximated(d, *inst);
            warn("MeshPart geometry is not imported: MeshId keeps the rbxassetid:// text (put <id>.glb in the "
                 "assets folder to link it)");
        }
        if (auto v = text_of(d, "TextureID")) inst->set("TextureId", WValue::string(*v));
    } else if (is_script_class(cls)) {
        inst = convert_script(idx, parent, name);
    } else if (cls == "Animation") {
        inst = dm_.create_internal(cls, parent, name);
        if (auto v = text_of(d, "AnimationId")) inst->set("AnimationId", WValue::string(*v));
    } else if (is_gui_class(cls) || is_gui_extra(cls)) {
        const bool native = is_gui_class(cls) || cls == "TextBox";
        const char* tefra_cls = cls == "TextBox" ? "TextBox" : (is_gui_class(cls) ? cls.c_str() : "Frame");
        inst = dm_.create_internal(tefra_cls, parent, name);
        apply_gui_common(d, *inst);
        if (cls == "TextLabel" || cls == "TextBox" || is_button(cls)) apply_text(d, *inst);
        if (cls == "ImageLabel" || cls == "ImageButton") apply_image(d, *inst);
        if (is_button(cls))
            if (auto v = bool_of(d, "AutoButtonColor")) inst->set("AutoButtonColor", WValue::boolean(*v));
        if (!native) mark_approximated(d, *inst);
    } else if (is_ui_modifier(cls)) {
        inst = dm_.create_internal(cls, parent, name);
        apply_modifier(d, *inst);
        dump_props(d, *inst, "rbx.");
    } else if (cls == "SurfaceLight") {
        inst = dm_.create_internal("PointLight", parent, name);
        apply_light(d, *inst);
        mark_approximated(d, *inst);
    } else if (cls == "Tool" || cls == "Accessory" || cls == "Hat" || cls == "HopperBin") {
        inst = dm_.create_internal("Model", parent, name);
        mark_approximated(d, *inst);
    } else if (cls == "Terrain") {
        warn("voxel Terrain (SmoothGrid) is not converted: kept as a Folder with its settings in rbx.* attributes");
    }

    if (!inst) {
        inst = placeholder(idx, parent, name);
    } else {
        ++rep_.converted;
        if (opt_.keep_all_properties) dump_props(d, *inst, "rbx.");
    }
    apply_extras(d, *inst);
    made_[idx] = inst;
    convert_children(idx, *inst);
    return inst;
}

// A class Tefra does not have: a Folder with the same name, RobloxClass and every property as rbx.<Prop>. Its
// children are still converted, so nothing below it is lost.
Instance* Converter::placeholder(u32 idx, Instance* parent, const std::string& name) {
    const InstanceData& d = doc_.instances[idx];
    Instance* inst = dm_.create_internal("Folder", parent, name);
    ++rep_.imported_as_unsupported;
    ++rep_.unsupported_classes[d.class_name];
    inst->set_attribute("RobloxClass", WValue::string(d.class_name));
    dump_props(d, *inst, "rbx.");
    return inst;
}

// A class converted to the nearest Tefra class: RobloxClass and the properties as rbx.<Prop> keep what is lost.
void Converter::mark_approximated(const InstanceData& d, Instance& inst) {
    inst.set_attribute("RobloxClass", WValue::string(d.class_name));
    dump_props(d, inst, "rbx.");
    ++rep_.approximated;
}

// Every property of a source instance as an attribute (Name, Tags and AttributesSerialize excluded).
void Converter::dump_props(const InstanceData& d, Instance& inst, const std::string& prefix) {
    for (const Property& p : d.props) {
        if (p.name == "Name" || p.name == "Tags" || p.name == "AttributesSerialize") continue;
        if (std::optional<WValue> v = attribute_value(p.value)) inst.set_attribute(prefix + p.name, std::move(*v));
    }
}

// Roblox Attributes and CollectionService tags of an instance.
void Converter::apply_extras(const InstanceData& d, Instance& inst) {
    if (const Value* a = d.find("AttributesSerialize");
        a && (a->type == Type::BinaryString || a->type == Type::String)) {
        std::vector<std::pair<std::string, WValue>> attrs;
        if (decode_attributes(a->s, attrs)) {
            for (auto& [name, v] : attrs)
                if (inst.set_attribute(name, v)) ++rep_.attributes;
        } else {
            warn("Attributes with an unknown layout are kept as rbx.AttributesSerialize (base64)");
            inst.set_attribute("rbx.AttributesSerialize", WValue::string(truncated_base64(a->s)));
        }
    }
    if (const Value* t = d.find("Tags"); t && (t->type == Type::BinaryString || t->type == Type::String)) {
        for (const std::string& tag : split_tags(t->s))
            if (inst.add_tag(tag)) ++rep_.tags;
    }
}

// A property as a Tefra attribute value: numbers, bools, strings, Vector3 and Color3 keep their type; anything else
// becomes its JSON text. Blobs are truncated. Values with no Tefra meaning (none, NaN) give nothing.
std::optional<WValue> Converter::attribute_value(const Value& v) const {
    switch (v.type) {
    case Type::None: return std::nullopt;
    case Type::Bool: return WValue::boolean(v.i != 0);
    case Type::Int32:
    case Type::Int64:
    case Type::Enum:
    case Type::BrickColor: return WValue::integer(v.i);
    case Type::Float32:
    case Type::Float64:
        if (std::isfinite(v.d)) return WValue::number(v.d);
        break;
    case Type::String:
    case Type::ProtectedString:
    case Type::UniqueId: return WValue::string(v.s);
    case Type::BinaryString:
    case Type::Bytecode: return WValue::string(truncated_base64(v.s));
    case Type::Vector3: return WValue::vector3(vec3{v.f[0], v.f[1], v.f[2]});
    case Type::Color3: return WValue::color3(vec3{v.f[0], v.f[1], v.f[2]});
    case Type::Color3uint8: return WValue::color3(vec3{v.f[0] / 255.0f, v.f[1] / 255.0f, v.f[2] / 255.0f});
    default: break;
    }
    return WValue::string(value_json(v).dump());
}

// The asset file of a Roblox asset text ("rbxassetid://123" or "123") in the assets folder, or "" when there is none.
std::string Converter::asset_path(const std::string& text, std::initializer_list<const char*> exts) const {
    if (assets_dir_.empty()) return {};
    std::string id = text;
    if (id.rfind("rbxassetid://", 0) == 0) id = id.substr(13);
    if (id.empty() || id.size() > 20) return {};
    for (char c : id)
        if (c < '0' || c > '9') return {};
    for (const char* ext : exts) {
        std::string path = assets_dir_ + "/" + id + ext;
        if (file_exists(path.c_str())) return path;
    }
    return {};
}

// The file of a FileMesh SpecialMesh child of a Part (a glTF in the assets folder), or "".
std::string Converter::special_mesh_file(u32 idx) const {
    for (u32 c : doc_.instances[idx].children) {
        const InstanceData& cd = doc_.instances[c];
        if (cd.class_name != "SpecialMesh") continue;
        if (std::optional<std::string> id = text_of(cd, "MeshId")) {
            std::string file = asset_path(*id, {".glb", ".gltf"});
            if (!file.empty()) return file;
        }
    }
    return {};
}

// C# class of a script: its base name, plus the file index when another script has the same base.
std::string Converter::script_class(const std::string& name, u32 index) const {
    const std::string base = csharp_base(name);
    auto it = script_names_.find(base);
    return (it != script_names_.end() && it->second > 1) ? base + "_" + std::to_string(index) : base;
}

// Position (scaled to metres) and orientation of a CFrame property.
void Converter::apply_cframe(const InstanceData& d, Instance& inst, const char* prop, const char* pos,
                             const char* ori) {
    const Value* cf = nullptr;
    if (!cframe_of(d, prop, cf)) return;
    inst.set(pos, WValue::vector3(vec3{static_cast<f32>(cf->f[0] * s_), static_cast<f32>(cf->f[1] * s_),
                                       static_cast<f32>(cf->f[2] * s_)}));
    inst.set(ori, WValue::vector3(orientation_of(mat_from_cframe(cf->f))));
}

void Converter::apply_decal(const InstanceData& d, Instance& inst) {
    std::string tex = text_of(d, "Texture").value_or(std::string());
    if (!tex.empty()) {
        const std::string file = asset_path(tex, {".png", ".jpg", ".jpeg"});
        if (!file.empty()) {
            tex = file;
            ++rep_.assets_linked;
        }
    }
    inst.set("Texture", WValue::string(tex));
    if (auto f = int_of(d, "Face")) inst.set("Face", WValue::enumeration(enum_index("NormalId", face_name(*f))));
    if (auto v = num_of(d, "Transparency")) inst.set("Transparency", WValue::number(*v));
    if (auto c = color_of(d, "Color3")) inst.set("Color3", WValue::color3(*c));
    if (inst.class_name() == "Texture") {
        if (auto v = num_of(d, "StudsPerTileU")) inst.set("StudsPerTileU", WValue::number(*v * s_));
        if (auto v = num_of(d, "StudsPerTileV")) inst.set("StudsPerTileV", WValue::number(*v * s_));
    }
}

void Converter::apply_value(const InstanceData& d, Instance& inst) {
    const std::string& cls = inst.class_name();
    if (cls == "StringValue") {
        if (auto v = text_of(d, "Value")) inst.set("Value", WValue::string(*v));
    } else if (cls == "IntValue") {
        if (auto v = int_of(d, "Value")) inst.set("Value", WValue::integer(*v));
    } else if (cls == "NumberValue") {
        if (auto v = num_of(d, "Value")) inst.set("Value", WValue::number(*v));
    } else if (cls == "BoolValue") {
        if (auto v = bool_of(d, "Value")) inst.set("Value", WValue::boolean(*v));
    } else if (cls == "Vector3Value") {
        if (auto v = vec3_of(d, "Value")) inst.set("Value", WValue::vector3(*v * f32(s_)));
    } else if (cls == "Color3Value") {
        if (auto c = color_of(d, "Value")) inst.set("Value", WValue::color3(*c));
    } else if (cls == "ObjectValue") {
        defer_ref(&inst, "Value", d.find("Value"));
    } else if (cls == "CFrameValue") {
        apply_cframe(d, inst, "Value", "Position", "Orientation");
    }
}

// Roblox UI modifiers with a Tefra class: the numeric and colour properties map; the rest is kept as rbx.* attributes.
void Converter::apply_modifier(const InstanceData& d, Instance& inst) {
    const std::string& cls = inst.class_name();
    auto udim_offset = [&](const char* prop) -> std::optional<f32> {
        const Value* v = d.find(prop);
        if (!v || v->type != Type::UDim) return std::nullopt;
        return v->f[1];
    };
    if (cls == "UIStroke") {
        if (auto c = color_of(d, "Color")) inst.set("Color", WValue::color3(*c));
        if (auto v = num_of(d, "Thickness")) inst.set("Thickness", WValue::number(*v));
        if (auto v = num_of(d, "Transparency")) inst.set("Transparency", WValue::number(*v));
    } else if (cls == "UIPadding") {
        for (const char* side : {"PaddingLeft", "PaddingRight", "PaddingTop", "PaddingBottom"})
            if (auto v = udim_offset(side)) inst.set(side, WValue::number(*v));
    } else if (cls == "UIListLayout") {
        if (auto v = udim_offset("Padding")) inst.set("Padding", WValue::number(*v));
    } else if (cls == "UIGridLayout") {
        if (auto s = udim2_of(d, "CellSize")) inst.set("CellSize", WValue::vector3(vec3{(*s)[1], (*s)[3], 0.0f}));
        if (auto s = udim2_of(d, "CellPadding")) inst.set("CellPadding", WValue::vector3(vec3{(*s)[1], (*s)[3], 0.0f}));
    } else if (cls == "UIGradient") {
        const Value* seq = d.find("Color");
        if (seq && seq->type == Type::ColorSequence && seq->seq.size() >= 5) {
            const usize n = seq->seq.size();
            inst.set("Color0", WValue::color3({seq->seq[1], seq->seq[2], seq->seq[3]}));
            inst.set("Color1", WValue::color3({seq->seq[n - 4], seq->seq[n - 3], seq->seq[n - 2]}));
        }
        if (auto v = num_of(d, "Rotation")) inst.set("Rotation", WValue::number(*v));
    } else if (cls == "UIAspectRatioConstraint") {
        if (auto v = num_of(d, "AspectRatio")) inst.set("AspectRatio", WValue::number(*v));
    }
}

void Converter::apply_service(const InstanceData& d, Instance& svc) {
    const std::string& cls = svc.class_name();
    if (cls == "Workspace") {
        if (auto g = num_of(d, "Gravity")) svc.set("Gravity", WValue::number(*g * s_)); // time-preserving scale
        if (auto h = num_of(d, "FallenPartsDestroyHeight"))
            svc.set("FallenPartsDestroyHeight", WValue::number(*h * s_));
        defer_ref(&svc, "CurrentCamera", d.find("CurrentCamera"));
    } else if (cls == "Lighting") {
        if (auto t = num_of(d, "ClockTime")) {
            svc.set("ClockTime", WValue::number(*t));
        } else if (auto tod = text_of(d, "TimeOfDay")) {
            int h = 0, m = 0, sec = 0;
            if (std::sscanf(tod->c_str(), "%d:%d:%d", &h, &m, &sec) >= 2)
                svc.set("ClockTime", WValue::number(h + m / 60.0 + sec / 3600.0));
        }
        if (auto v = num_of(d, "GeographicLatitude")) svc.set("GeographicLatitude", WValue::number(*v));
        if (auto v = num_of(d, "ExposureCompensation")) svc.set("ExposureCompensation", WValue::number(*v));
        if (auto fog_end = num_of(d, "FogEnd")) {
            // Fog that is fully opaque at FogEnd: extinction 3 / distance (1/m at sea level).
            const f64 distance = std::max(1.0, *fog_end * s_);
            svc.set("FogDensity", WValue::number(std::min(1.0, 3.0 / distance)));
        }
    } else if (cls == "StarterPlayer") {
        if (auto v = num_of(d, "CharacterWalkSpeed")) svc.set("CharacterWalkSpeed", WValue::number(*v * s_));
        if (auto h = num_of(d, "CharacterJumpHeight")) {
            svc.set("CharacterJumpHeight", WValue::number(*h * s_));
        } else if (auto jp = num_of(d, "CharacterJumpPower")) {
            svc.set("CharacterJumpHeight", WValue::number(*jp * *jp / (2.0 * kRobloxGravity) * s_));
        }
        if (auto v = num_of(d, "CameraMaxZoomDistance")) svc.set("CameraMaxZoomDistance", WValue::number(*v * s_));
    } else if (cls == "Players") {
        if (auto v = num_of(d, "RespawnTime")) svc.set("RespawnTime", WValue::number(*v));
    } else if (cls == "SoundService") {
        if (auto v = num_of(d, "Volume")) svc.set("Volume", WValue::number(*v));
        if (auto v = num_of(d, "RolloffScale")) svc.set("RolloffScale", WValue::number(*v));
    } else if (cls == "StarterGui") {
        if (auto v = bool_of(d, "ShowDevelopmentGui")) svc.set("ShowDevelopmentGui", WValue::boolean(*v));
    }
}

// Shared BasePart properties: CFrame, Size, colour, material, physics and flags.
void Converter::apply_base(const InstanceData& d, Instance& inst, bool cylinder) {
    vec3 pos{0, 0, 0};
    Mat3 rot;
    const Value* cf = nullptr;
    if (cframe_of(d, "CFrame", cf)) {
        pos = vec3{static_cast<f32>(cf->f[0] * s_), static_cast<f32>(cf->f[1] * s_), static_cast<f32>(cf->f[2] * s_)};
        rot = mat_from_cframe(cf->f);
    }
    if (cylinder) rot = mat_mul(rot, cylinder_axis_fix());
    inst.set("Position", WValue::vector3(pos));
    inst.set("Orientation", WValue::vector3(orientation_of(rot)));

    vec3 size{4, 1, 2}; // Roblox default part size (studs)
    if (auto sz = vec3_of(d, "size")) size = *sz;
    if (cylinder) size = vec3{size.y, size.x, size.z};
    inst.set("Size", WValue::vector3(vec3{static_cast<f32>(size.x * s_), static_cast<f32>(size.y * s_),
                                          static_cast<f32>(size.z * s_)}));

    bool unknown_brick = false;
    if (auto c = part_color(d, unknown_brick)) inst.set("Color", WValue::color3(*c));
    if (unknown_brick) warn("unknown BrickColor number: a medium stone grey is used instead");

    if (auto token = int_of(d, "Material")) {
        const char* name = material_name(*token);
        if (!name) {
            warn("unknown Material number: Plastic is used instead");
            name = "Plastic";
        }
        inst.set("Material", WValue::enumeration(enum_index("Material", name)));
    }
    if (auto v = num_of(d, "Transparency")) inst.set("Transparency", WValue::number(*v));
    if (auto v = num_of(d, "Reflectance")) inst.set("Reflectance", WValue::number(*v));
    if (auto v = bool_of(d, "CastShadow")) inst.set("CastShadow", WValue::boolean(*v));
    if (auto v = bool_of(d, "Anchored")) inst.set("Anchored", WValue::boolean(*v));
    if (auto v = bool_of(d, "CanCollide")) inst.set("CanCollide", WValue::boolean(*v));
    if (auto v = bool_of(d, "CanTouch")) inst.set("CanTouch", WValue::boolean(*v));
    if (auto v = bool_of(d, "Massless")) inst.set("Massless", WValue::boolean(*v));

    if (const Value* p = d.find("CustomPhysicalProperties"); p && p->type == Type::PhysicalProperties && p->present) {
        // Density: kg per stud^3 -> kg per m^3 (the same mass in a smaller volume).
        inst.set("Density", WValue::number(static_cast<f64>(p->f[0]) / (s_ * s_ * s_)));
        inst.set("Friction", WValue::number(static_cast<f64>(p->f[1])));
        inst.set("Elasticity", WValue::number(static_cast<f64>(p->f[2])));
    }
}

Instance* Converter::convert_part(u32 idx, Instance* parent, const std::string& name) {
    const InstanceData& d = doc_.instances[idx];
    const std::string& cls = d.class_name;
    if (cls == "Part") {
        // A Part with a FileMesh SpecialMesh whose file exists in the assets folder is a mesh part.
        const std::string file = special_mesh_file(idx);
        if (!file.empty()) {
            Instance* mesh = dm_.create_internal("MeshPart", parent, name);
            apply_base(d, *mesh, false);
            mesh->set("MeshId", WValue::string(file));
            ++rep_.assets_linked;
            return mesh;
        }
    }
    const bool spawn = cls == "SpawnLocation";
    Instance* inst = dm_.create_internal(spawn ? "SpawnLocation" : "Part", parent, name);

    const char* shape = "Block";
    if (cls == "WedgePart") {
        shape = "Wedge";
        warn("WedgePart keeps its orientation (assumed equal to Tefra's Wedge: verify on a sample)");
    } else {
        const i64 token = int_of(d, "shape").value_or(1);
        switch (token) {
        case 0: shape = "Ball"; break;
        case 1: shape = "Block"; break;
        case 2: shape = "Cylinder"; break;
        case 3: shape = "Wedge"; break;
        default: warn("Part shape not in the table (CornerWedge, Prism...): converted to a Block"); break;
        }
    }
    if (cls != "Part" && cls != "SpawnLocation" && cls != "WedgePart")
        warn(cls + " is converted to the nearest Tefra Part shape (its own geometry is not kept)");
    inst->set("Shape", WValue::enumeration(enum_index("PartType", shape)));
    const bool cylinder = std::string_view(shape) == "Cylinder";
    apply_base(d, *inst, cylinder);
    if (spawn) {
        if (auto v = bool_of(d, "Enabled")) inst->set("Enabled", WValue::boolean(*v));
        if (auto v = bool_of(d, "Neutral")) inst->set("Neutral", WValue::boolean(*v));
    }
    if (cls != "Part" && !spawn) mark_approximated(d, *inst);
    return inst;
}

Instance* Converter::convert_script(u32 idx, Instance* parent, const std::string& name) {
    const InstanceData& d = doc_.instances[idx];
    Instance* inst = dm_.create_internal(d.class_name, parent, name);
    const std::string luau = text_of(d, "Source").value_or(std::string());
    inst->set("LuauSource", WValue::string(luau));
    inst->set("Source", WValue::string(script_skeleton(d.class_name, name, luau, script_class(name, idx))));
    if (d.class_name != "ModuleScript") {
        const bool disabled_in_file = bool_of(d, "Disabled").value_or(false);
        inst->set("Enabled", WValue::boolean(opt_.convert_scripts_disabled ? false : !disabled_in_file));
    }
    if (d.class_name == "Script") {
        // Roblox RunContext: 2 = Client; everything else runs on the server.
        const i64 ctx = int_of(d, "RunContext").value_or(0);
        inst->set("RunContext", WValue::enumeration(enum_index("RunContext", ctx == 2 ? "Client" : "Server")));
    }
    ++rep_.scripts;
    warn("Luau scripts keep their code in LuauSource; each gets a disabled C# skeleton in Source");
    return inst;
}

void Converter::apply_light(const InstanceData& d, Instance& inst) {
    if (auto c = color_of(d, "Color")) inst.set("Color", WValue::color3(*c));
    if (auto v = num_of(d, "Brightness")) inst.set("Brightness", WValue::number(*v));
    if (auto v = num_of(d, "Range")) inst.set("Range", WValue::number(*v * s_));
    if (auto v = bool_of(d, "Shadows")) inst.set("Shadows", WValue::boolean(*v));
    if (auto v = bool_of(d, "Enabled")) inst.set("Enabled", WValue::boolean(*v));
    if (inst.class_name() == "SpotLight")
        if (auto v = num_of(d, "Angle")) inst.set("Angle", WValue::number(*v));
}

void Converter::apply_humanoid(const InstanceData& d, Instance& inst) {
    if (auto v = num_of(d, "Health")) inst.set("Health", WValue::number(*v));
    if (auto v = num_of(d, "MaxHealth")) inst.set("MaxHealth", WValue::number(*v));
    if (auto v = num_of(d, "WalkSpeed")) inst.set("WalkSpeed", WValue::number(*v * s_));
    if (auto h = num_of(d, "JumpHeight")) {
        inst.set("JumpHeight", WValue::number(*h * s_));
    } else if (auto jp = num_of(d, "JumpPower")) {
        inst.set("JumpHeight", WValue::number(*jp * *jp / (2.0 * kRobloxGravity) * s_));
    }
    if (auto v = num_of(d, "HipHeight")) inst.set("HipHeight", WValue::number(*v * s_));
    if (auto v = bool_of(d, "AutoRotate")) inst.set("AutoRotate", WValue::boolean(*v));
    if (auto v = text_of(d, "DisplayName")) inst.set("DisplayName", WValue::string(*v));
    if (auto rig = int_of(d, "RigType")) {
        // Roblox: 0 = R6, 1 = R15.
        if (*rig == 1) {
            inst.set("RigType", WValue::enumeration(enum_index("RigType", "R15")));
        } else {
            warn("R6 humanoids are converted to the Custom rig (only R15 is supported)");
            inst.set("RigType", WValue::enumeration(enum_index("RigType", "Custom")));
        }
    }
}

void Converter::apply_camera(const InstanceData& d, Instance& inst) {
    const Value* cf = nullptr;
    if (cframe_of(d, "CFrame", cf)) {
        const f32 px = static_cast<f32>(cf->f[0] * s_), py = static_cast<f32>(cf->f[1] * s_),
                  pz = static_cast<f32>(cf->f[2] * s_);
        const Mat3 rot = mat_from_cframe(cf->f);
        constexpr f64 kLookDistance = 10.0; // metres in front of the camera (CFrame has no look target)
        // The look direction is the negative back column (Roblox forward is -Z).
        const f32 lx = static_cast<f32>(-rot.m[0][2] * kLookDistance);
        const f32 ly = static_cast<f32>(-rot.m[1][2] * kLookDistance);
        const f32 lz = static_cast<f32>(-rot.m[2][2] * kLookDistance);
        inst.set("Position", WValue::vector3(vec3{px, py, pz}));
        inst.set("LookAt", WValue::vector3(vec3{px + lx, py + ly, pz + lz}));
    }
    if (auto fov = num_of(d, "FieldOfView")) inst.set("FieldOfView", WValue::number(*fov));
}

void Converter::apply_gui_common(const InstanceData& d, Instance& inst) {
    if (auto p = udim2_of(d, "Position")) {
        inst.set("Position", WValue::vector3(vec3{(*p)[0], (*p)[2], 0.0f}));
        inst.set("PositionOffset", WValue::vector3(vec3{(*p)[1], (*p)[3], 0.0f}));
    }
    if (auto sz = udim2_of(d, "Size")) {
        inst.set("Size", WValue::vector3(vec3{(*sz)[0], (*sz)[2], 0.0f}));
        inst.set("SizeOffset", WValue::vector3(vec3{(*sz)[1], (*sz)[3], 0.0f}));
    }
    if (auto a = vec2_of(d, "AnchorPoint")) inst.set("AnchorPoint", WValue::vector3(*a));
    if (auto v = num_of(d, "Rotation")) inst.set("Rotation", WValue::number(*v));
    if (auto c = color_of(d, "BackgroundColor3")) inst.set("BackgroundColor", WValue::color3(*c));
    if (auto v = num_of(d, "BackgroundTransparency")) inst.set("BackgroundTransparency", WValue::number(*v));
    if (auto c = color_of(d, "BorderColor3")) inst.set("BorderColor", WValue::color3(*c));
    if (auto v = num_of(d, "BorderSizePixel")) inst.set("BorderSize", WValue::number(*v));
    if (auto v = bool_of(d, "Visible")) inst.set("Visible", WValue::boolean(*v));
    if (auto v = int_of(d, "ZIndex")) inst.set("ZIndex", WValue::integer(*v));
}

void Converter::apply_text(const InstanceData& d, Instance& inst) {
    if (auto v = text_of(d, "Text")) inst.set("Text", WValue::string(*v));
    if (auto c = color_of(d, "TextColor3")) inst.set("TextColor", WValue::color3(*c));
    if (auto v = num_of(d, "TextSize")) inst.set("TextSize", WValue::number(*v));
    if (auto v = bool_of(d, "TextScaled")) inst.set("TextScaled", WValue::boolean(*v));
    if (auto v = num_of(d, "TextTransparency")) inst.set("TextTransparency", WValue::number(*v));
    if (auto v = num_of(d, "TextStrokeTransparency")) inst.set("TextStrokeTransparency", WValue::number(*v));
    if (auto x = int_of(d, "TextXAlignment")) {
        // Roblox: Left 0, Right 1, Center 2.
        const char* name = *x == 0 ? "Left" : (*x == 1 ? "Right" : "Center");
        inst.set("TextXAlignment", WValue::enumeration(enum_index("TextXAlignment", name)));
    }
    if (auto y = int_of(d, "TextYAlignment")) {
        // Roblox: Top 0, Center 1, Bottom 2.
        const char* name = *y == 0 ? "Top" : (*y == 2 ? "Bottom" : "Center");
        inst.set("TextYAlignment", WValue::enumeration(enum_index("TextYAlignment", name)));
    }
    const Value* face = d.find("FontFace");
    if (face && face->type == Type::Font) {
        const bool mono = face->s.find("Mono") != std::string::npos || face->s.find("Code") != std::string::npos;
        const char* name = mono ? "Mono" : (face->i >= 600 ? "Bold" : "Default");
        inst.set("Font", WValue::enumeration(enum_index("Font", name)));
    } else if (auto f = int_of(d, "Font")) {
        // Roblox Font tokens: 2 ArialBold, 4 SourceSansBold, 10 Code.
        const char* name = (*f == 2 || *f == 4) ? "Bold" : (*f == 10 ? "Mono" : "Default");
        inst.set("Font", WValue::enumeration(enum_index("Font", name)));
    }
}

void Converter::apply_image(const InstanceData& d, Instance& inst) {
    if (auto v = text_of(d, "Image")) inst.set("Image", WValue::string(*v));
    if (auto c = color_of(d, "ImageColor3")) inst.set("ImageColor", WValue::color3(*c));
    if (auto v = num_of(d, "ImageTransparency")) inst.set("ImageTransparency", WValue::number(*v));
    if (auto t = int_of(d, "ScaleType")) {
        // Roblox ScaleType: Stretch 0, Slice 1, Tile 2, Fit 3, Crop 4.
        const char* name = "Stretch";
        if (*t == 3)
            name = "Fit";
        else if (*t == 4)
            name = "Crop";
        else if (*t != 0)
            warn("ScaleType Slice/Tile is converted to Stretch");
        inst.set("ScaleType", WValue::enumeration(enum_index("ScaleType", name)));
    }
}

void Converter::apply_corner(const InstanceData& d, Instance& gui) {
    const Value* r = d.find("CornerRadius");
    if (r && r->type == Type::UDim) {
        if (r->f[0] != 0.0f) warn("UICorner scale is ignored (only the pixel radius is kept)");
        gui.set("CornerRadius", WValue::number(static_cast<f64>(r->f[1])));
    }
}

void Converter::apply_clouds(const InstanceData& d, Instance& lighting) {
    if (auto cover = num_of(d, "Cover")) lighting.set("CloudCover", WValue::number(std::clamp(*cover, 0.0, 1.0)));
}

JsonValue Converter::value_json(const Value& v) const {
    auto ref_name = [&](i64 index) -> JsonValue {
        if (index < 0 || static_cast<usize>(index) >= doc_.instances.size()) return JsonValue();
        return JsonValue(doc_.instances[static_cast<usize>(index)].name());
    };
    switch (v.type) {
    case Type::Bool: return JsonValue(v.i != 0);
    case Type::Int32:
    case Type::Int64:
    case Type::Enum:
    case Type::BrickColor:
    case Type::Faces:
    case Type::Axes: return JsonValue(v.i);
    case Type::Float32:
    case Type::Float64: return num_json(v.d);
    case Type::String:
    case Type::ProtectedString:
    case Type::UniqueId: return JsonValue(v.s);
    case Type::Bytecode:
    case Type::BinaryString: return JsonValue(truncated_base64(v.s));
    case Type::UDim: return floats_json(v.f, 2);
    case Type::UDim2: return floats_json(v.f, 4);
    case Type::Ray: return floats_json(v.f, 6);
    case Type::Vector2: return floats_json(v.f, 2);
    case Type::Vector3:
    case Type::Vector3int16:
    case Type::Color3:
    case Type::Color3uint8: return floats_json(v.f, 3);
    case Type::CFrame: return floats_json(v.f, 12);
    case Type::OptionalCFrame: return v.present ? floats_json(v.f, 12) : JsonValue();
    case Type::NumberRange: return floats_json(v.f, 2);
    case Type::Rect: return floats_json(v.f, 4);
    case Type::PhysicalProperties: return v.present ? floats_json(v.f, 6) : JsonValue();
    case Type::NumberSequence:
    case Type::ColorSequence: {
        JsonValue a = JsonValue::array();
        for (f32 x : v.seq) a.push(num_json(static_cast<f64>(x)));
        return a;
    }
    case Type::Font: {
        JsonValue o = JsonValue::object();
        o.set("family", JsonValue(v.s));
        o.set("weight", JsonValue(v.i));
        o.set("italic", JsonValue(v.aux == 1));
        return o;
    }
    case Type::Content:
        if (v.aux == 1) return JsonValue(v.s);
        if (v.aux == 2) return ref_name(v.i);
        return JsonValue();
    case Type::Ref: return ref_name(v.i);
    case Type::SharedString:
        if (v.i < 0 || static_cast<usize>(v.i) >= doc_.shared_strings.size()) return JsonValue();
        return JsonValue(truncated_base64(doc_.shared_strings[static_cast<usize>(v.i)]));
    case Type::None: break;
    }
    return JsonValue();
}

// Resets the services to empty (their properties back to the defaults) before a place is imported.
void clear_services(DataModel& dm) {
    const std::vector<Instance*> services = dm.root().children();
    for (Instance* s : services) {
        while (!s->children().empty()) dm.destroy(s->children().back());
        for (u32 i = 1; i < s->cls().prop_count(); ++i)
            if (const world::PropInfo* p = s->cls().prop(i)) s->set(i, p->def, true);
    }
}

// The options of a file: when no assets folder is named, "<dir>/<stem>_assets" next to the file.
ImportOptions options_for_path(const char* path, const ImportOptions& options) {
    ImportOptions o = options;
    if (!o.assets_dir.empty() || !path) return o;
    const std::string p = path;
    const usize slash = p.find_last_of("/\\");
    const usize start = slash == std::string::npos ? 0 : slash + 1;
    usize end = p.find_last_of('.');
    if (end == std::string::npos || end <= start) end = p.size();
    o.assets_dir = p.substr(0, start) + p.substr(start, end - start) + "_assets";
    return o;
}

bool check_bytes(const u8* data, usize size, std::string* error) {
    if (data && size) return true;
    if (error) *error = "empty input";
    return false;
}

} // namespace

bool import_place_memory(const u8* data, usize size, DataModel& dm, const ImportOptions& options, ImportReport* report,
                         std::string* error) {
    if (!check_bytes(data, size, error)) return false;
    Document doc;
    if (!parse_document(data, size, doc, error)) return false; // the model is untouched on a parse error
    ImportReport local;
    ImportReport& rep = report ? *report : local;
    rep = ImportReport{};
    clear_services(dm);
    Converter conv(doc, dm, options, rep);
    conv.run_place();
    return true;
}

bool import_place(const char* path, DataModel& dm, const ImportOptions& options, ImportReport* report,
                  std::string* error) {
    std::vector<u8> bytes;
    if (!path || !file_read_all(path, bytes)) {
        if (error) *error = std::string("cannot read ") + (path ? path : "(null)");
        return false;
    }
    return import_place_memory(bytes.data(), bytes.size(), dm, options_for_path(path, options), report, error);
}

bool import_model_memory(const u8* data, usize size, Instance* parent, const ImportOptions& options,
                         ImportReport* report, std::string* error, std::vector<Instance*>* roots) {
    if (!parent) {
        if (error) *error = "no parent instance";
        return false;
    }
    if (!check_bytes(data, size, error)) return false;
    Document doc;
    if (!parse_document(data, size, doc, error)) return false;
    ImportReport local;
    ImportReport& rep = report ? *report : local;
    rep = ImportReport{};
    if (roots) roots->clear();
    Converter conv(doc, parent->model(), options, rep);
    conv.run_model(*parent, roots);
    return true;
}

bool import_model(const char* path, Instance* parent, const ImportOptions& options, ImportReport* report,
                  std::string* error, std::vector<Instance*>* roots) {
    std::vector<u8> bytes;
    if (!path || !file_read_all(path, bytes)) {
        if (error) *error = std::string("cannot read ") + (path ? path : "(null)");
        return false;
    }
    return import_model_memory(bytes.data(), bytes.size(), parent, options_for_path(path, options), report, error,
                               roots);
}

JsonValue report_to_json(const ImportReport& report) {
    JsonValue o = JsonValue::object();
    o.set("instances", JsonValue(report.instances));
    o.set("converted", JsonValue(report.converted));
    o.set("imported_as_unsupported", JsonValue(report.imported_as_unsupported));
    o.set("approximated", JsonValue(report.approximated));
    o.set("scripts", JsonValue(report.scripts));
    o.set("attributes", JsonValue(report.attributes));
    o.set("tags", JsonValue(report.tags));
    o.set("assets_linked", JsonValue(report.assets_linked));
    JsonValue warnings = JsonValue::array();
    for (const std::string& w : report.warnings) warnings.push(JsonValue(w));
    o.set("warnings", std::move(warnings));
    std::vector<std::pair<std::string, u32>> classes(report.unsupported_classes.begin(),
                                                     report.unsupported_classes.end());
    std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    JsonValue uc = JsonValue::object();
    for (const auto& [name, count] : classes) uc.set(name, JsonValue(count));
    o.set("unsupported_classes", std::move(uc));
    std::vector<std::pair<std::string, u32>> all(report.class_counts.begin(), report.class_counts.end());
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    JsonValue cc = JsonValue::object();
    for (const auto& [name, count] : all) cc.set(name, JsonValue(count));
    o.set("class_counts", std::move(cc));
    return o;
}

std::string report_summary(const ImportReport& report) {
    return std::to_string(report.instances) + " instances: " + std::to_string(report.converted) + " converted (" +
           std::to_string(report.scripts) + " scripts), " + std::to_string(report.imported_as_unsupported) +
           " kept as placeholders, " + std::to_string(report.warnings.size()) + " warnings";
}

} // namespace tefra::rbx
