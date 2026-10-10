#pragma once

// Decoded Roblox place / model file (ADR-017: the user's OWN places only). The instance tree with typed property
// values, independent of the container (binary .rbxl/.rbxm or XML .rbxlx/.rbxmx). Written clean-room from the public
// format description of the rbx-dom project (summary in docs/research/m3b-rbx/FORMAT.md). This layer knows nothing
// about the Tefra DataModel: tefra/rbx/importer.h converts a Document into one.
//
// Every reader is defensive: all sizes and counts are bounds-checked against the input, decompression is capped and
// a malformed file makes the parse return false with an error. It never crashes and never reads out of bounds.

#include "tefra/core/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tefra::rbx {

// Value types. The binary type ids are listed in FORMAT.md; the XML element names map to the same set.
enum class Type : u8 {
    None,
    String,
    Bool,
    Int32,
    Float32,
    Float64,
    UDim,
    UDim2,
    Ray,
    Faces,
    Axes,
    BrickColor,
    Color3,
    Vector2,
    Vector3,
    CFrame,
    Enum,
    Ref,
    Vector3int16,
    NumberSequence,
    ColorSequence,
    NumberRange,
    Rect,
    PhysicalProperties,
    Color3uint8,
    Int64,
    SharedString,
    Bytecode,
    OptionalCFrame,
    UniqueId,
    Font,
    Content,
    BinaryString,
    ProtectedString,
};
const char* type_name(Type t);

// A property value. Field use by type:
//   String, ProtectedString, BinaryString, Bytecode: s (raw bytes; never interpreted)
//   Bool, Int32, Int64, Enum, BrickColor, Faces, Axes: i
//   Float32, Float64: d
//   UDim: f[0] scale, f[1] offset.  UDim2: f[0] X.scale, f[1] X.offset, f[2] Y.scale, f[3] Y.offset
//   Vector2: f[0..1].  Vector3, Vector3int16: f[0..2].  Color3: f[0..2] in 0-1.  Color3uint8: f[0..2] in 0-255
//   CFrame, OptionalCFrame: f[0..2] position, f[3..11] rotation R00 R01 R02 R10 R11 R12 R20 R21 R22 (row-major;
//       column 0 is the right vector, column 2 the back vector). An empty OptionalCFrame has present = false.
//   Ray: f[0..2] origin, f[3..5] direction.  NumberRange: f[0..1].  Rect: f[0..3] min.x, min.y, max.x, max.y
//   PhysicalProperties: present = custom; f[0..5] density, friction, elasticity, friction weight, elasticity
//       weight, acoustic absorption
//   NumberSequence: seq = (time, value, envelope)*.  ColorSequence: seq = (time, r, g, b, envelope)*
//   Ref: i = index in Document::instances, -1 = nil
//   SharedString: i = index in Document::shared_strings, -1 = unknown
//   Font: s = family URI, i = weight (100-900), aux = style (0 normal, 1 italic)
//   Content: aux = source (0 none, 1 uri, 2 object); s = uri; i = instance index for objects (-1 otherwise)
//   UniqueId: s = 32 lowercase hex digits
struct Value {
    Type type = Type::None;
    bool present = true;
    u8 aux = 0;
    i64 i = 0;
    f64 d = 0.0;
    f32 f[12] = {};
    std::string s;
    std::vector<f32> seq;
};

struct Property {
    std::string name; // serialized name ("size", "Color3uint8", "Health_XML"...), not always the Lua name
    Value value;
};

struct InstanceData {
    std::string class_name;
    bool service = false; // marked as a service in the file
    bool dropped = false; // not part of the tree (nesting beyond kMaxDepth)
    i32 parent = -1;      // index in Document::instances; -1 = root
    std::vector<u32> children;
    std::vector<Property> props;

    // Case-insensitive lookup of a property by serialized name (nullptr when absent).
    const Value* find(std::string_view name) const;
    // The Name property, or the class name when it is missing or empty.
    const std::string& name() const;
};

struct Document {
    std::vector<InstanceData> instances;
    std::vector<u32> roots; // children of the DataModel (place) or of the insertion point (model), in file order
    std::vector<std::string> shared_strings;
    std::vector<std::pair<std::string, std::string>> meta;
    std::vector<std::string> warnings; // problems that did not stop the parse (skipped chunks, unknown types...)
};

enum class Format : u8 { Unknown, Binary, Xml };

// Limits (defence against malformed or hostile files).
constexpr u32 kMaxDepth = 512;                    // deeper instances are dropped with a warning
constexpr u32 kMaxInstances = 4u * 1024u * 1024u; // per file
constexpr u64 kMaxChunkBytes = 256ull << 20;      // decompressed size of one binary chunk
constexpr u64 kMaxTotalBytes = 1024ull << 20;     // decompressed size of all chunks together

// Binary files start with "<roblox!" + 89 FF 0D 0A 1A 0A; XML files with "<roblox" (after an optional BOM / XML
// declaration). Anything else is Unknown.
Format detect_format(const u8* data, usize size);
// Parsers: on failure they return false, fill `error` and leave `out` in an unspecified (but valid) state.
bool parse_binary(const u8* data, usize size, Document& out, std::string* error = nullptr);
bool parse_xml(const u8* data, usize size, Document& out, std::string* error = nullptr);
bool parse_document(const u8* data, usize size, Document& out, std::string* error = nullptr); // detects the format

// Decompressors used by the binary reader (exposed for tests). Both require the exact decompressed size.
bool lz4_decompress_block(const u8* src, usize src_size, u8* dst, usize dst_size);
bool zstd_decompress_frame(const u8* src, usize src_size, u8* dst, usize dst_size);

// RFC 4648 base64 (the XML format's BinaryString / SharedString). Decoding skips whitespace and stops at '='.
std::string base64_encode(const u8* data, usize size);
bool base64_decode(std::string_view text, std::string& out);

// Rotation matrix (row-major R00..R22) of one of the 24 axis-aligned binary CFrame ids; false for unknown ids.
bool cframe_rotation_from_id(u8 id, f32 out[9]);

} // namespace tefra::rbx
