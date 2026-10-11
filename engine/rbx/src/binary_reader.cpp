// Binary place/model reader (.rbxl / .rbxm). Layout (FORMAT.md): a 32-byte header, then chunks META, SSTR, INST*,
// PROP*, PRNT, END. Each chunk has a 16-byte header (name, compressed size, decompressed size, reserved) and is
// stored raw, LZ4-block compressed or as a zstd frame. Numeric arrays are big-endian and byte-interleaved; integers
// are zigzag-encoded, floats have the sign bit moved to the end, referent arrays are delta-encoded.
#include "internal.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace tefra::rbx {

namespace {

constexpr u64 kMaxValues = 16ull * 1024ull * 1024ull; // property values per file (memory bound)

struct Reader {
    const u8* p = nullptr;
    usize n = 0;
    usize pos = 0;
    bool ok = true;

    usize left() const { return n - pos; }
    bool need(usize k) {
        if (!ok || k > n - pos) {
            ok = false;
            return false;
        }
        return true;
    }
    const u8* take(usize k) {
        if (!need(k)) return nullptr;
        const u8* r = p + pos;
        pos += k;
        return r;
    }
    u8 byte() { return need(1) ? p[pos++] : u8(0); }
    u16 u16le() {
        if (!need(2)) return 0;
        const u16 v = static_cast<u16>(p[pos] | (p[pos + 1] << 8));
        pos += 2;
        return v;
    }
    u32 u32le() {
        if (!need(4)) return 0;
        const u32 v = u32(p[pos]) | (u32(p[pos + 1]) << 8) | (u32(p[pos + 2]) << 16) | (u32(p[pos + 3]) << 24);
        pos += 4;
        return v;
    }
    f32 f32le() {
        const u32 b = u32le();
        f32 f;
        std::memcpy(&f, &b, sizeof f);
        return f;
    }
    bool str(std::string& out) {
        const u32 len = u32le();
        const u8* b = take(len);
        if (!b) return false;
        out.assign(reinterpret_cast<const char*>(b), len);
        return true;
    }
};

i32 unzigzag32(u32 v) { return static_cast<i32>((v >> 1) ^ (0u - (v & 1u))); }
i64 unzigzag64(u64 v) { return static_cast<i64>((v >> 1) ^ (0ull - (v & 1ull))); }
f32 rbx_f32(u32 v) {
    const u32 bits = (v >> 1) | (v << 31); // "eeeeeeee m...m s" -> IEEE "s eeeeeeee m...m"
    f32 f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// `count` big-endian words of `width` (1-8) bytes stored byte-interleaved (all first bytes, then all second...).
bool interleaved(Reader& r, usize count, usize width, std::vector<u64>& out) {
    if (!r.ok || width == 0 || count > r.left() / width) {
        r.ok = false;
        return false;
    }
    const u8* b = r.p + r.pos;
    out.assign(count, 0);
    for (usize k = 0; k < count; ++k) {
        u64 v = 0;
        for (usize j = 0; j < width; ++j) v = (v << 8) | b[j * count + k];
        out[k] = v;
    }
    r.pos += count * width;
    return true;
}

bool i32_array(Reader& r, usize count, std::vector<i32>& out) {
    std::vector<u64> w;
    if (!interleaved(r, count, 4, w)) return false;
    out.resize(count);
    for (usize k = 0; k < count; ++k) out[k] = unzigzag32(static_cast<u32>(w[k]));
    return true;
}

bool f32_array(Reader& r, usize count, std::vector<f32>& out) {
    std::vector<u64> w;
    if (!interleaved(r, count, 4, w)) return false;
    out.resize(count);
    for (usize k = 0; k < count; ++k) out[k] = rbx_f32(static_cast<u32>(w[k]));
    return true;
}

// Referents: Int32 array, then a running sum (each stored value is the difference to the previous referent).
bool ref_array(Reader& r, usize count, std::vector<i32>& out) {
    if (!i32_array(r, count, out)) return false;
    u32 acc = 0;
    for (usize k = 0; k < count; ++k) {
        acc += static_cast<u32>(out[k]); // wraps like the 32-bit writer; no signed overflow
        out[k] = static_cast<i32>(acc);
    }
    return true;
}

bool cframes(Reader& r, usize n, std::vector<Value>& out, u32& unknown_ids) {
    if (n > r.left()) {
        r.ok = false;
        return false;
    }
    static const f32 kIdentity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (usize k = 0; k < n; ++k) {
        Value& v = out[k];
        v.type = Type::CFrame;
        const u8 id = r.byte();
        if (!r.ok) return false;
        if (id == 0) {
            for (int j = 0; j < 9; ++j) v.f[3 + j] = r.f32le();
        } else if (!cframe_rotation_from_id(id, v.f + 3)) {
            std::memcpy(v.f + 3, kIdentity, sizeof kIdentity);
            ++unknown_ids;
        }
    }
    std::vector<f32> x, y, z;
    if (!f32_array(r, n, x) || !f32_array(r, n, y) || !f32_array(r, n, z)) return false;
    for (usize k = 0; k < n; ++k) {
        out[k].f[0] = x[k];
        out[k].f[1] = y[k];
        out[k].f[2] = z[k];
    }
    return r.ok;
}

std::string hex(const u8* b, usize n) {
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (usize i = 0; i < n; ++i) {
        s += kHex[b[i] >> 4];
        s += kHex[b[i] & 15];
    }
    return s;
}

struct ClassEntry {
    std::string name;
    std::vector<u32> inst; // instance index per class slot (kNoInstance for duplicated referents)
};
constexpr u32 kNoInstance = 0xffffffffu;

struct BinaryParser {
    Document& doc;
    std::unordered_map<u32, ClassEntry> classes;
    std::unordered_map<i32, u32> by_ref;
    std::vector<u32> prnt_order;
    u64 values = 0;
    u32 unknown_rotation_ids = 0;
    std::string err;

    bool fail(std::string e) {
        if (err.empty()) err = std::move(e);
        return false;
    }

    bool meta(Reader& c) {
        const u32 count = c.u32le();
        for (u32 k = 0; k < count && c.ok; ++k) {
            std::string key, value;
            if (!c.str(key) || !c.str(value)) break;
            doc.meta.emplace_back(std::move(key), std::move(value));
        }
        return c.ok || fail("malformed META chunk");
    }

    bool sstr(Reader& c) {
        c.u32le(); // version (0)
        const u32 count = c.u32le();
        if (!c.ok || count > c.left() / 20) return fail("malformed SSTR chunk");
        doc.shared_strings.reserve(count);
        for (u32 k = 0; k < count; ++k) {
            c.take(16); // MD5 of the string (unused)
            std::string s;
            if (!c.str(s)) return fail("malformed SSTR chunk");
            doc.shared_strings.push_back(std::move(s));
        }
        return true;
    }

    bool inst(Reader& c) {
        const u32 class_id = c.u32le();
        std::string name;
        c.str(name);
        const u8 format = c.byte();
        const u32 count = c.u32le();
        if (!c.ok || count > c.left() / 4) return fail("malformed INST chunk");
        if (doc.instances.size() + count > kMaxInstances) return fail("too many instances");
        std::vector<i32> refs;
        if (!ref_array(c, count, refs)) return fail("malformed INST chunk");
        // Service markers (format 1) follow; their content carries no extra information.
        if (classes.count(class_id)) {
            detail::warn(doc, "duplicate INST chunk for class id " + std::to_string(class_id) + " ignored");
            return true;
        }
        ClassEntry e;
        e.name = name;
        e.inst.reserve(count);
        u32 dup = 0;
        for (i32 ref : refs) {
            if (by_ref.count(ref)) {
                e.inst.push_back(kNoInstance);
                ++dup;
                continue;
            }
            const u32 idx = static_cast<u32>(doc.instances.size());
            InstanceData d;
            d.class_name = name;
            d.service = format == 1;
            doc.instances.push_back(std::move(d));
            by_ref.emplace(ref, idx);
            e.inst.push_back(idx);
        }
        if (dup) detail::warn(doc, name + ": " + std::to_string(dup) + " duplicated referents ignored");
        classes.emplace(class_id, std::move(e));
        return true;
    }

    // Decodes `n` values of binary type `type`. Ref and Content object values keep the raw referent in `i` until
    // resolve_refs().
    bool decode(Reader& c, u8 type, usize n, std::vector<Value>& out) {
        if (n > c.left()) return false; // every type takes at least one byte per value
        out.assign(n, Value{});
        std::vector<f32> a, b, cc, dd;
        std::vector<i32> ia, ib;
        std::vector<u64> w;
        switch (type) {
        case 0x01: // String
        case 0x1d: // Bytecode (kept as opaque bytes, never interpreted)
            for (Value& v : out) {
                v.type = type == 0x01 ? Type::String : Type::Bytecode;
                if (!c.str(v.s)) return false;
            }
            return true;
        case 0x02: { // Bool
            const u8* p = c.take(n);
            if (!p) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Bool;
                out[k].i = p[k] != 0;
            }
            return true;
        }
        case 0x03: // Int32
            if (!i32_array(c, n, ia)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Int32;
                out[k].i = ia[k];
            }
            return true;
        case 0x04: // Float32
            if (!f32_array(c, n, a)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Float32;
                out[k].d = a[k];
            }
            return true;
        case 0x05: { // Float64, little-endian, not interleaved
            if (n > c.left() / 8) return false;
            for (Value& v : out) {
                const u8* p = c.take(8);
                u64 bits = 0;
                for (int j = 7; j >= 0; --j) bits = (bits << 8) | p[j];
                v.type = Type::Float64;
                std::memcpy(&v.d, &bits, sizeof v.d);
            }
            return true;
        }
        case 0x06: // UDim: scales then offsets
            if (!f32_array(c, n, a) || !i32_array(c, n, ia)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::UDim;
                out[k].f[0] = a[k];
                out[k].f[1] = static_cast<f32>(ia[k]);
            }
            return true;
        case 0x07: // UDim2: X.scale, Y.scale, X.offset, Y.offset
            if (!f32_array(c, n, a) || !f32_array(c, n, b) || !i32_array(c, n, ia) || !i32_array(c, n, ib))
                return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::UDim2;
                out[k].f[0] = a[k];
                out[k].f[1] = static_cast<f32>(ia[k]);
                out[k].f[2] = b[k];
                out[k].f[3] = static_cast<f32>(ib[k]);
            }
            return true;
        case 0x08: // Ray: 6 little-endian floats each
            if (n > c.left() / 24) return false;
            for (Value& v : out) {
                v.type = Type::Ray;
                for (int j = 0; j < 6; ++j) v.f[j] = c.f32le();
            }
            return true;
        case 0x09:   // Faces
        case 0x0a: { // Axes
            const u8* p = c.take(n);
            if (!p) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = type == 0x09 ? Type::Faces : Type::Axes;
                out[k].i = p[k];
            }
            return true;
        }
        case 0x0b: // BrickColor: interleaved big-endian u32
        case 0x12: // Enum
        case 0x1c: // SharedString index
            if (!interleaved(c, n, 4, w)) return false;
            for (usize k = 0; k < n; ++k) {
                Value& v = out[k];
                v.type = type == 0x0b ? Type::BrickColor : (type == 0x12 ? Type::Enum : Type::SharedString);
                v.i = static_cast<i64>(w[k]);
                if (type == 0x1c && w[k] >= doc.shared_strings.size()) v.i = -1;
            }
            return true;
        case 0x0c: // Color3
        case 0x0e: // Vector3
            if (!f32_array(c, n, a) || !f32_array(c, n, b) || !f32_array(c, n, cc)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = type == 0x0c ? Type::Color3 : Type::Vector3;
                out[k].f[0] = a[k];
                out[k].f[1] = b[k];
                out[k].f[2] = cc[k];
            }
            return true;
        case 0x0d: // Vector2
            if (!f32_array(c, n, a) || !f32_array(c, n, b)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Vector2;
                out[k].f[0] = a[k];
                out[k].f[1] = b[k];
            }
            return true;
        case 0x10: // CFrame
            return cframes(c, n, out, unknown_rotation_ids);
        case 0x13: // Referent
            if (!ref_array(c, n, ia)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Ref;
                out[k].i = ia[k];
            }
            return true;
        case 0x14: // Vector3int16: 3 little-endian i16, not interleaved
            if (n > c.left() / 6) return false;
            for (Value& v : out) {
                v.type = Type::Vector3int16;
                for (int j = 0; j < 3; ++j) v.f[j] = static_cast<f32>(static_cast<i16>(c.u16le()));
            }
            return true;
        case 0x15:   // NumberSequence: count + (time, value, envelope)*
        case 0x16: { // ColorSequence: count + (time, r, g, b, envelope)*
            const usize stride = type == 0x15 ? 3 : 5;
            for (Value& v : out) {
                v.type = type == 0x15 ? Type::NumberSequence : Type::ColorSequence;
                const u32 count = c.u32le();
                if (!c.ok || count > c.left() / (4 * stride)) return false;
                v.seq.resize(count * stride);
                for (f32& f : v.seq) f = c.f32le();
            }
            return true;
        }
        case 0x17: // NumberRange: 2 little-endian floats
            if (n > c.left() / 8) return false;
            for (Value& v : out) {
                v.type = Type::NumberRange;
                v.f[0] = c.f32le();
                v.f[1] = c.f32le();
            }
            return true;
        case 0x18: // Rect: min.x, min.y, max.x, max.y arrays
            if (!f32_array(c, n, a) || !f32_array(c, n, b) || !f32_array(c, n, cc) || !f32_array(c, n, dd))
                return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Rect;
                out[k].f[0] = a[k];
                out[k].f[1] = b[k];
                out[k].f[2] = cc[k];
                out[k].f[3] = dd[k];
            }
            return true;
        case 0x19: // PhysicalProperties: flags (bit 0 custom, bit 1 has acoustic absorption) + floats
            for (Value& v : out) {
                v.type = Type::PhysicalProperties;
                const u8 flags = c.byte();
                v.present = (flags & 1) != 0;
                if (v.present) {
                    for (int j = 0; j < 5; ++j) v.f[j] = c.f32le();
                    v.f[5] = (flags & 2) ? c.f32le() : 1.0f;
                }
                if (!c.ok) return false;
            }
            return true;
        case 0x1a: { // Color3uint8: R array, G array, B array (bytes)
            const u8* p = c.take(n * 3);
            if (!p) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Color3uint8;
                out[k].f[0] = p[k];
                out[k].f[1] = p[n + k];
                out[k].f[2] = p[2 * n + k];
            }
            return true;
        }
        case 0x1b: // Int64
            if (!interleaved(c, n, 8, w)) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::Int64;
                out[k].i = unzigzag64(w[k]);
            }
            return true;
        case 0x1e: { // OptionalCFrame: 0x10, CFrame array, 0x02, Bool array
            if (c.byte() != 0x10 || !cframes(c, n, out, unknown_rotation_ids) || c.byte() != 0x02) return false;
            const u8* p = c.take(n);
            if (!p) return false;
            for (usize k = 0; k < n; ++k) {
                out[k].type = Type::OptionalCFrame;
                out[k].present = p[k] != 0;
            }
            return true;
        }
        case 0x1f: { // UniqueId: 16 bytes each, interleaved
            if (n > c.left() / 16) return false;
            const u8* p = c.take(n * 16);
            u8 tmp[16];
            for (usize k = 0; k < n; ++k) {
                for (usize j = 0; j < 16; ++j) tmp[j] = p[j * n + k];
                out[k].type = Type::UniqueId;
                out[k].s = hex(tmp, 16);
            }
            return true;
        }
        case 0x20: // Font: family, weight (u16), style (u8), cached face id
            for (Value& v : out) {
                v.type = Type::Font;
                std::string cached;
                c.str(v.s);
                v.i = c.u16le();
                v.aux = c.byte();
                c.str(cached);
                if (!c.ok) return false;
            }
            return true;
        case 0x22: { // Content: source types, then URIs, object referents and external referents
            if (!interleaved(c, n, 4, w)) return false;
            const u32 uri_count = c.u32le();
            if (!c.ok || uri_count > c.left() / 4) return false;
            std::vector<std::string> uris(uri_count);
            for (std::string& u : uris)
                if (!c.str(u)) return false;
            const u32 obj_count = c.u32le();
            if (!c.ok || obj_count > c.left() / 4 || !ref_array(c, obj_count, ia)) return false;
            const u32 ext_count = c.u32le();
            if (!c.ok || ext_count > c.left() / 4 || !ref_array(c, ext_count, ib)) return false;
            usize ui = 0, oi = 0;
            for (usize k = 0; k < n; ++k) {
                Value& v = out[k];
                v.type = Type::Content;
                v.i = -1;
                if (w[k] == 1 && ui < uris.size()) {
                    v.aux = 1;
                    v.s = std::move(uris[ui++]);
                } else if (w[k] == 2 && oi < ia.size()) {
                    v.aux = 2;
                    v.i = ia[oi++];
                }
            }
            return true;
        }
        default: return false; // unknown type id: the chunk is skipped
        }
    }

    bool prop(Reader& c) {
        const u32 class_id = c.u32le();
        std::string name;
        c.str(name);
        const u8 type = c.byte();
        if (!c.ok) return fail("malformed PROP chunk");
        auto it = classes.find(class_id);
        if (it == classes.end()) {
            detail::warn(doc, "PROP '" + name + "' for an unknown class id ignored");
            return true;
        }
        ClassEntry& e = it->second;
        const usize n = e.inst.size();
        values += n;
        if (values > kMaxValues) return fail("too many property values");
        std::vector<Value> vals;
        if (!decode(c, type, n, vals) || !c.ok) {
            char t[8];
            std::snprintf(t, sizeof t, "0x%02x", type);
            detail::warn(doc, e.name + "." + name + ": malformed or unsupported value type " + t + "; skipped");
            return true;
        }
        for (usize k = 0; k < n; ++k)
            if (e.inst[k] != kNoInstance) doc.instances[e.inst[k]].props.push_back({name, std::move(vals[k])});
        return true;
    }

    bool prnt(Reader& c) {
        c.byte(); // version (0)
        const u32 count = c.u32le();
        if (!c.ok || count > c.left() / 8) return fail("malformed PRNT chunk");
        std::vector<i32> child, parent;
        if (!ref_array(c, count, child) || !ref_array(c, count, parent)) return fail("malformed PRNT chunk");
        u32 unknown = 0;
        prnt_order.reserve(count);
        for (usize k = 0; k < count; ++k) {
            auto ci = by_ref.find(child[k]);
            if (ci == by_ref.end()) {
                ++unknown;
                continue;
            }
            i32 p = -1;
            if (parent[k] != -1) {
                auto pi = by_ref.find(parent[k]);
                if (pi != by_ref.end())
                    p = static_cast<i32>(pi->second);
                else
                    ++unknown;
            }
            doc.instances[ci->second].parent = p;
            prnt_order.push_back(ci->second);
        }
        if (unknown) detail::warn(doc, "PRNT: " + std::to_string(unknown) + " unknown referents ignored");
        return true;
    }

    // Referent values -> instance indices.
    void resolve_refs() {
        for (InstanceData& d : doc.instances)
            for (Property& p : d.props) {
                Value& v = p.value;
                if (v.type != Type::Ref && !(v.type == Type::Content && v.aux == 2)) continue;
                if (v.type == Type::Ref && v.i == -1) continue;
                auto it = by_ref.find(static_cast<i32>(v.i));
                v.i = it == by_ref.end() ? -1 : static_cast<i64>(it->second);
            }
    }
};

} // namespace

bool parse_binary(const u8* data, usize size, Document& out, std::string* error) {
    out = Document{};
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    if (detect_format(data, size) != Format::Binary) return fail("not a binary Roblox file");
    if (size < 32) return fail("truncated header");
    const u16 version = static_cast<u16>(data[14] | (data[15] << 8));
    if (version != 0) return fail("unsupported binary format version " + std::to_string(version));

    BinaryParser bp{out, {}, {}, {}, 0, 0, {}};
    Reader file{data, size, 32, true};
    std::vector<u8> buffer;
    u64 total = 0;
    bool got_end = false, got_prnt = false;
    while (file.left() > 0 && !got_end) {
        if (file.left() < 16) return fail("truncated chunk header");
        const u8* hdr = file.take(4);
        char name_buf[5] = {static_cast<char>(hdr[0]), static_cast<char>(hdr[1]), static_cast<char>(hdr[2]),
                            static_cast<char>(hdr[3]), 0};
        const std::string name(name_buf); // shorter names are zero-padded
        const u32 compressed = file.u32le();
        const u32 length = file.u32le();
        file.u32le(); // reserved
        const u8* chunk = nullptr;
        if (compressed == 0) {
            chunk = file.take(length);
            if (!chunk) return fail("truncated " + name + " chunk");
        } else {
            if (length > kMaxChunkBytes || total + length > kMaxTotalBytes) return fail(name + " chunk too large");
            const u8* src = file.take(compressed);
            if (!src) return fail("truncated " + name + " chunk");
            buffer.resize(length);
            const bool zstd = compressed >= 4 && src[0] == 0x28 && src[1] == 0xb5 && src[2] == 0x2f && src[3] == 0xfd;
            const bool ok = zstd ? zstd_decompress_frame(src, compressed, buffer.data(), length)
                                 : lz4_decompress_block(src, compressed, buffer.data(), length);
            if (!ok) return fail(std::string("cannot decompress ") + (zstd ? "zstd" : "LZ4") + " chunk " + name);
            chunk = buffer.data();
        }
        total += length;
        if (total > kMaxTotalBytes) return fail("file too large");
        Reader c{chunk, length, 0, true};
        bool ok = true;
        if (name == "META")
            ok = bp.meta(c);
        else if (name == "SSTR")
            ok = bp.sstr(c);
        else if (name == "INST")
            ok = bp.inst(c);
        else if (name == "PROP")
            ok = bp.prop(c);
        else if (name == "PRNT") {
            ok = bp.prnt(c);
            got_prnt = true;
        } else if (name == "END")
            got_end = true;
        else
            detail::warn(out, "unknown chunk '" + name + "' skipped");
        if (!ok) return fail(bp.err);
    }
    if (!got_end) return fail("truncated file (no END chunk)");
    if (!got_prnt && !out.instances.empty()) detail::warn(out, "no PRNT chunk: every instance is a root");
    if (bp.unknown_rotation_ids)
        detail::warn(out, std::to_string(bp.unknown_rotation_ids) + " CFrames with unknown rotation ids (identity)");
    bp.resolve_refs();
    detail::finalize_tree(out, bp.prnt_order);
    return true;
}

} // namespace tefra::rbx
