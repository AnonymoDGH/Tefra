// Model import (see model_import.h). FBX and OBJ (+MTL) are read by ufbx (third_party/ufbx, MIT or public domain);
// STL (ASCII and binary) and PLY (ASCII and binary little endian) by the small parsers below. Every format becomes an
// intermediate scene that is written as a binary glTF 2.0 file.
#include "tefra/renderer/model_import.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>

#include "meshoptimizer.h"
#include "stb_image_write.h"
#include "ufbx.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/json_value.h"

namespace tefra::render {
namespace {

constexpr u32 kNone = 0xffffffffu;

// ---------------------------------------------------------------------------------------------------------------------
// Intermediate scene

struct Prim {
    std::vector<f32> pos; // xyz
    std::vector<f32> nrm; // xyz, empty when the source has none
    std::vector<f32> uv;  // xy (glTF convention, v down), empty when absent
    std::vector<u8> col;  // rgba, empty when absent
    std::vector<f32> tan; // xyzw, filled when write_tangents
    std::vector<u32> idx; // triangle list

    u32 vertex_count() const { return static_cast<u32>(pos.size() / 3); }
};

struct ImpPrim {
    Prim geo;
    i32 material = -1;
};

struct ImpMesh {
    std::string name;
    std::vector<ImpPrim> prims;
};

struct ImpNode {
    std::string name;
    f32 matrix[16]{}; // column-major, glTF layout
    i32 mesh = -1;
    std::vector<u32> children;
};

struct ImpMaterial {
    std::string name;
    f32 base_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    i32 base_texture = -1;
    f32 metallic = 0.0f;
    f32 roughness = 0.6f;
    i32 metal_rough_texture = -1; // G = roughness, B = metallic (glTF)
    i32 normal_texture = -1;
    f32 emissive[3] = {0.0f, 0.0f, 0.0f};
    i32 emissive_texture = -1;
};

struct ImpTexture {
    std::vector<u8> bytes; // PNG or JPEG, as read or re-encoded
    const char* mime = "image/png";
    std::string stem; // suggested file name when textures are not embedded
};

struct ImpScene {
    std::string format;
    f32 root_matrix[16]{}; // applied above the roots (unit scale and up axis of OBJ, STL and PLY)
    std::vector<ImpMesh> meshes;
    std::vector<ImpMaterial> materials;
    std::vector<ImpTexture> textures;
    std::vector<ImpNode> nodes;
    std::vector<u32> roots;
};

void set_identity(f32* m) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

bool is_identity(const f32* m) {
    f32 id[16];
    set_identity(id);
    return std::memcmp(id, m, sizeof(id)) == 0;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string str_of(std::string_view s) { return std::string(s.data(), s.size()); }

u32 bits_of(f32 v) {
    u32 b = 0;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

f32 clamp01(f32 v) { return std::isfinite(v) ? std::min(1.0f, std::max(0.0f, v)) : 0.0f; }

// Mime type from the magic bytes. Only PNG and JPEG are decodable by the engine (stb_image is built for them).
const char* sniff_mime(const std::vector<u8>& b) {
    if (b.size() >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "image/png";
    if (b.size() >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return "image/jpeg";
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared geometry helpers

// Vertex normals from the triangles (area weighted), for sources without normals.
std::vector<f32> smooth_normals(const std::vector<f32>& pos, const std::vector<u32>& idx) {
    std::vector<f32> n(pos.size(), 0.0f);
    for (usize t = 0; t + 2 < idx.size(); t += 3) {
        const u32 a = idx[t], b = idx[t + 1], c = idx[t + 2];
        const f32* pa = &pos[a * 3];
        const f32* pb = &pos[b * 3];
        const f32* pc = &pos[c * 3];
        const f32 e1[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
        const f32 e2[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
        const f32 fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (u32 v : {a, b, c})
            for (int k = 0; k < 3; ++k) n[v * 3 + static_cast<u32>(k)] += fn[k];
    }
    for (usize i = 0; i + 2 < n.size(); i += 3) {
        const f32 len = std::sqrt(n[i] * n[i] + n[i + 1] * n[i + 1] + n[i + 2] * n[i + 2]);
        if (len > 1e-20f) {
            n[i] /= len;
            n[i + 1] /= len;
            n[i + 2] /= len;
        } else {
            n[i] = 0.0f;
            n[i + 1] = 1.0f;
            n[i + 2] = 0.0f;
        }
    }
    return n;
}

// Drops the vertices no triangle uses and renumbers the indices.
void compact(Prim& p) {
    const u32 count = p.vertex_count();
    std::vector<u32> remap(count, kNone);
    Prim q;
    u32 next = 0;
    for (u32& i : p.idx) {
        if (remap[i] == kNone) {
            remap[i] = next++;
            q.pos.insert(q.pos.end(), p.pos.begin() + i * 3, p.pos.begin() + i * 3 + 3);
            if (!p.nrm.empty()) q.nrm.insert(q.nrm.end(), p.nrm.begin() + i * 3, p.nrm.begin() + i * 3 + 3);
            if (!p.uv.empty()) q.uv.insert(q.uv.end(), p.uv.begin() + i * 2, p.uv.begin() + i * 2 + 2);
            if (!p.col.empty()) q.col.insert(q.col.end(), p.col.begin() + i * 4, p.col.begin() + i * 4 + 4);
        }
        i = remap[i];
    }
    q.idx = std::move(p.idx);
    p = std::move(q);
}

// TANGENT (xyz + handedness) from the UV gradients, accumulated per triangle and orthonormalized per vertex.
std::vector<f32> tangents_from_uv(const Prim& p) {
    const u32 n = p.vertex_count();
    std::vector<f32> t(static_cast<usize>(n) * 3, 0.0f), b(static_cast<usize>(n) * 3, 0.0f);
    for (usize k = 0; k + 2 < p.idx.size(); k += 3) {
        const u32 v[3] = {p.idx[k], p.idx[k + 1], p.idx[k + 2]};
        const f32* P[3] = {&p.pos[v[0] * 3], &p.pos[v[1] * 3], &p.pos[v[2] * 3]};
        const f32* U[3] = {&p.uv[v[0] * 2], &p.uv[v[1] * 2], &p.uv[v[2] * 2]};
        const f32 e1[3] = {P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]};
        const f32 e2[3] = {P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]};
        const f32 du1 = U[1][0] - U[0][0], dv1 = U[1][1] - U[0][1];
        const f32 du2 = U[2][0] - U[0][0], dv2 = U[2][1] - U[0][1];
        const f32 det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        const f32 r = 1.0f / det;
        for (int c = 0; c < 3; ++c) {
            const f32 tc = (e1[c] * dv2 - e2[c] * dv1) * r;
            const f32 bc = (e2[c] * du1 - e1[c] * du2) * r;
            for (u32 vi : v) {
                t[vi * 3 + static_cast<u32>(c)] += tc;
                b[vi * 3 + static_cast<u32>(c)] += bc;
            }
        }
    }
    std::vector<f32> out(static_cast<usize>(n) * 4, 0.0f);
    for (u32 i = 0; i < n; ++i) {
        const f32* nv = &p.nrm[i * 3];
        const f32* tv = &t[i * 3];
        const f32* bv = &b[i * 3];
        const f32 d = nv[0] * tv[0] + nv[1] * tv[1] + nv[2] * tv[2];
        f32 o[3] = {tv[0] - nv[0] * d, tv[1] - nv[1] * d, tv[2] - nv[2] * d};
        f32 len = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        if (len < 1e-12f) {
            // No usable UV gradient: any tangent orthogonal to the normal.
            // Cross product of a fixed axis (X or Y, whichever is not parallel) with the normal.
            const f32 ax = std::fabs(nv[0]) < 0.9f ? 1.0f : 0.0f;
            const f32 ay = 1.0f - ax;
            o[0] = ay * nv[2];
            o[1] = -ax * nv[2];
            o[2] = ax * nv[1] - ay * nv[0];
            len = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
            if (len < 1e-12f) len = 1.0f;
        }
        o[0] /= len;
        o[1] /= len;
        o[2] /= len;
        const f32 cx = nv[1] * o[2] - nv[2] * o[1];
        const f32 cy = nv[2] * o[0] - nv[0] * o[2];
        const f32 cz = nv[0] * o[1] - nv[1] * o[0];
        const f32 w = (cx * bv[0] + cy * bv[1] + cz * bv[2]) < 0.0f ? -1.0f : 1.0f;
        out[i * 4] = o[0];
        out[i * 4 + 1] = o[1];
        out[i * 4 + 2] = o[2];
        out[i * 4 + 3] = w;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// STL (ASCII and binary). Flat shading: each triangle keeps its normal, vertices are shared only when both the position
// and the normal are equal.

void add_flat_triangle(ImpPrim& prim, std::map<std::array<u32, 6>, u32>& welded, const f32 (&p)[3][3],
                       const f32* file_normal) {
    f32 n[3] = {file_normal[0], file_normal[1], file_normal[2]};
    f32 len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(len > 0.5f) || !std::isfinite(len)) {
        const f32 e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
        const f32 e2[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(len > 1e-20f)) {
            n[0] = 0.0f;
            n[1] = 1.0f;
            n[2] = 0.0f;
            len = 1.0f;
        }
    }
    n[0] /= len;
    n[1] /= len;
    n[2] /= len;
    for (int c = 0; c < 3; ++c) {
        const std::array<u32, 6> key = {bits_of(p[c][0]), bits_of(p[c][1]), bits_of(p[c][2]),
                                        bits_of(n[0]),    bits_of(n[1]),    bits_of(n[2])};
        auto it = welded.find(key);
        if (it == welded.end()) {
            const u32 v = prim.geo.vertex_count();
            it = welded.emplace(key, v).first;
            prim.geo.pos.insert(prim.geo.pos.end(), {p[c][0], p[c][1], p[c][2]});
            prim.geo.nrm.insert(prim.geo.nrm.end(), {n[0], n[1], n[2]});
        }
        prim.geo.idx.push_back(it->second);
    }
}

bool parse_stl(const std::vector<u8>& data, ImpScene& scene, std::string& error) {
    ImpPrim prim;
    std::map<std::array<u32, 6>, u32> welded;
    f32 tri[3][3] = {};
    if (data.size() >= 84) {
        const u32 count = static_cast<u32>(data[80]) | (static_cast<u32>(data[81]) << 8) |
                          (static_cast<u32>(data[82]) << 16) | (static_cast<u32>(data[83]) << 24);
        if (static_cast<u64>(84) + static_cast<u64>(count) * 50u == data.size()) {
            for (u32 i = 0; i < count; ++i) {
                const u8* rec = data.data() + 84 + static_cast<usize>(i) * 50;
                f32 v[12];
                std::memcpy(v, rec, sizeof(v)); // little endian hosts only (x86, ARM): the STL format is LE
                for (int c = 0; c < 3; ++c)
                    for (int k = 0; k < 3; ++k) tri[c][k] = v[3 + c * 3 + k];
                add_flat_triangle(prim, welded, tri, v);
            }
            if (count == 0) {
                error = "the STL file has no triangles";
                return false;
            }
            scene.meshes.push_back(ImpMesh{"stl", {std::move(prim)}});
            return true;
        }
    }
    // ASCII: "facet normal nx ny nz", "vertex x y z" x3.
    const char* p = reinterpret_cast<const char*>(data.data());
    const char* end = p + data.size();
    auto next = [&](std::string_view& w) {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p >= end) return false;
        const char* s = p;
        while (p < end && !std::isspace(static_cast<unsigned char>(*p))) ++p;
        w = std::string_view(s, static_cast<usize>(p - s));
        return true;
    };
    auto next_float = [&](f32& out) {
        std::string_view w;
        if (!next(w)) return false;
        const std::string tmp = str_of(w);
        char* e = nullptr;
        out = std::strtof(tmp.c_str(), &e);
        return e != tmp.c_str();
    };
    std::string_view w;
    f32 fn[3] = {0.0f, 0.0f, 0.0f};
    int nv = 0;
    while (next(w)) {
        if (w == "facet") {
            nv = 0;
            fn[0] = fn[1] = fn[2] = 0.0f;
            std::string_view k;
            const char* save = p;
            if (next(k) && k == "normal") {
                if (!next_float(fn[0]) || !next_float(fn[1]) || !next_float(fn[2])) {
                    error = "bad facet normal in the STL file";
                    return false;
                }
            } else {
                p = save; // not a normal: the token belongs to the next keyword
            }
        } else if (w == "vertex") {
            if (nv >= 3) {
                error = "bad STL facet: more than three vertices";
                return false;
            }
            if (!next_float(tri[nv][0]) || !next_float(tri[nv][1]) || !next_float(tri[nv][2])) {
                error = "bad vertex in the STL file";
                return false;
            }
            if (++nv == 3) add_flat_triangle(prim, welded, tri, fn);
        }
    }
    if (prim.geo.idx.empty()) {
        error = "the STL file has no triangles";
        return false;
    }
    scene.meshes.push_back(ImpMesh{"stl", {std::move(prim)}});
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// PLY (ASCII, binary little endian). Vertices: x y z, nx ny nz, s/t or u/v (and the texture_ variants), red green blue.
// Faces: vertex_indices (or vertex_index) lists, triangulated as fans. Other elements are read and ignored.

enum class PType : u8 { I8, U8, I16, U16, I32, U32, F32, F64 };

bool ply_type(std::string_view s, PType& t) {
    if (s == "char" || s == "int8")
        t = PType::I8;
    else if (s == "uchar" || s == "uint8")
        t = PType::U8;
    else if (s == "short" || s == "int16")
        t = PType::I16;
    else if (s == "ushort" || s == "uint16")
        t = PType::U16;
    else if (s == "int" || s == "int32")
        t = PType::I32;
    else if (s == "uint" || s == "uint32")
        t = PType::U32;
    else if (s == "float" || s == "float32")
        t = PType::F32;
    else if (s == "double" || s == "float64")
        t = PType::F64;
    else
        return false;
    return true;
}

usize ply_size(PType t) {
    switch (t) {
    case PType::I8:
    case PType::U8: return 1;
    case PType::I16:
    case PType::U16: return 2;
    case PType::I32:
    case PType::U32:
    case PType::F32: return 4;
    case PType::F64: return 8;
    }
    return 1;
}

struct PlyProp {
    std::string name;
    PType type = PType::F32;
    bool list = false;
    PType count_type = PType::U8;
};

struct PlyElem {
    std::string name;
    u64 count = 0;
    std::vector<PlyProp> props;
};

// Reads values from the body, either binary little endian or ASCII tokens.
class PlyReader {
  public:
    PlyReader(const u8* p, const u8* end, bool ascii) : p_(p), end_(end), ascii_(ascii) {}

    bool read(PType t, double& out) {
        if (ascii_) {
            while (p_ < end_ && std::isspace(static_cast<unsigned char>(*p_))) ++p_;
            if (p_ >= end_) return false;
            const u8* s = p_;
            while (p_ < end_ && !std::isspace(static_cast<unsigned char>(*p_))) ++p_;
            const std::string tmp(reinterpret_cast<const char*>(s), static_cast<usize>(p_ - s));
            char* e = nullptr;
            out = std::strtod(tmp.c_str(), &e);
            return e != tmp.c_str();
        }
        const usize n = ply_size(t);
        if (static_cast<usize>(end_ - p_) < n) return false;
        u8 b[8];
        std::memcpy(b, p_, n);
        p_ += n;
        switch (t) {
        case PType::I8: out = static_cast<i8>(b[0]); break;
        case PType::U8: out = b[0]; break;
        case PType::I16: {
            i16 v;
            std::memcpy(&v, b, 2);
            out = v;
            break;
        }
        case PType::U16: {
            u16 v;
            std::memcpy(&v, b, 2);
            out = v;
            break;
        }
        case PType::I32: {
            i32 v;
            std::memcpy(&v, b, 4);
            out = v;
            break;
        }
        case PType::U32: {
            u32 v;
            std::memcpy(&v, b, 4);
            out = v;
            break;
        }
        case PType::F32: {
            f32 v;
            std::memcpy(&v, b, 4);
            out = v;
            break;
        }
        case PType::F64: {
            f64 v;
            std::memcpy(&v, b, 8);
            out = v;
            break;
        }
        }
        return true;
    }

  private:
    const u8* p_;
    const u8* end_;
    bool ascii_;
};

bool parse_ply(const std::vector<u8>& data, ImpScene& scene, std::string& error, std::vector<std::string>& warn) {
    // Header: lines up to end_header.
    const std::string_view all(reinterpret_cast<const char*>(data.data()), data.size());
    const usize eh = all.find("end_header");
    if (all.substr(0, 3) != "ply" || eh == std::string_view::npos) {
        error = "not a PLY file";
        return false;
    }
    usize body = all.find('\n', eh);
    if (body == std::string_view::npos) {
        error = "truncated PLY header";
        return false;
    }
    ++body;
    bool ascii = false;
    std::vector<PlyElem> elems;
    usize hpos = 0;
    while (hpos < eh) {
        usize nl = all.find('\n', hpos);
        if (nl == std::string_view::npos || nl > body) nl = body - 1;
        std::string_view line = all.substr(hpos, nl - hpos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        hpos = nl + 1;
        // Tokenize the line.
        std::vector<std::string_view> tok;
        usize i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            const usize s = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            if (i > s) tok.push_back(line.substr(s, i - s));
        }
        if (tok.empty()) continue;
        if (tok[0] == "format") {
            if (tok.size() < 2) {
                error = "bad PLY format line";
                return false;
            }
            if (tok[1] == "ascii")
                ascii = true;
            else if (tok[1] == "binary_little_endian")
                ascii = false;
            else {
                error = "PLY big endian is not supported";
                return false;
            }
        } else if (tok[0] == "element" && tok.size() >= 3) {
            PlyElem e;
            e.name = str_of(tok[1]);
            e.count = std::strtoull(str_of(tok[2]).c_str(), nullptr, 10);
            elems.push_back(std::move(e));
        } else if (tok[0] == "property" && !elems.empty()) {
            PlyProp pr;
            if (tok.size() >= 5 && tok[1] == "list") {
                if (!ply_type(tok[2], pr.count_type) || !ply_type(tok[3], pr.type)) {
                    error = "bad PLY list property";
                    return false;
                }
                pr.list = true;
                pr.name = str_of(tok[4]);
            } else if (tok.size() >= 3) {
                if (!ply_type(tok[1], pr.type)) {
                    error = "bad PLY property type";
                    return false;
                }
                pr.name = str_of(tok[2]);
            } else {
                error = "bad PLY property";
                return false;
            }
            elems.back().props.push_back(pr);
        }
    }

    const u8* p = data.data() + body;
    const u8* end = data.data() + data.size();
    PlyReader rd(p, end, ascii);

    std::vector<f32> pos, nrm, uv;
    std::vector<u8> col;
    std::vector<u32> idx;
    bool has_n = false, has_uv = false, has_c = false;
    bool have_face_list = false;

    for (const PlyElem& e : elems) {
        const bool is_vertex = e.name == "vertex";
        const bool is_face = e.name == "face";
        // Column roles for vertices.
        int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1, iu = -1, iv = -1, ir = -1, ig = -1, ib = -1;
        if (is_vertex) {
            for (usize k = 0; k < e.props.size(); ++k) {
                const std::string& n = e.props[k].name;
                const int ki = static_cast<int>(k);
                if (n == "x")
                    ix = ki;
                else if (n == "y")
                    iy = ki;
                else if (n == "z")
                    iz = ki;
                else if (n == "nx")
                    inx = ki;
                else if (n == "ny")
                    iny = ki;
                else if (n == "nz")
                    inz = ki;
                else if (n == "s" || n == "u" || n == "texture_u" || n == "texture_s")
                    iu = ki;
                else if (n == "t" || n == "v" || n == "texture_v" || n == "texture_t")
                    iv = ki;
                else if (n == "red")
                    ir = ki;
                else if (n == "green")
                    ig = ki;
                else if (n == "blue")
                    ib = ki;
            }
            if (ix < 0 || iy < 0 || iz < 0) {
                error = "PLY vertices without x, y, z";
                return false;
            }
            has_n = inx >= 0 && iny >= 0 && inz >= 0;
            has_uv = iu >= 0 && iv >= 0;
            has_c = ir >= 0 && ig >= 0 && ib >= 0;
        }
        std::vector<double> row(e.props.size(), 0.0);
        std::vector<u32> poly;
        for (u64 n = 0; n < e.count; ++n) {
            for (usize k = 0; k < e.props.size(); ++k) {
                const PlyProp& pr = e.props[k];
                if (!pr.list) {
                    if (!rd.read(pr.type, row[k])) {
                        error = "truncated PLY data";
                        return false;
                    }
                    continue;
                }
                double cnt = 0.0;
                if (!rd.read(pr.count_type, cnt)) {
                    error = "truncated PLY data";
                    return false;
                }
                const bool take = is_face && (pr.name == "vertex_indices" || pr.name == "vertex_index");
                if (take) poly.clear();
                for (u64 j = 0; j < static_cast<u64>(cnt); ++j) {
                    double v = 0.0;
                    if (!rd.read(pr.type, v)) {
                        error = "truncated PLY data";
                        return false;
                    }
                    if (take) poly.push_back(static_cast<u32>(v));
                }
                if (take) {
                    have_face_list = true;
                    for (usize t = 1; t + 1 < poly.size(); ++t) {
                        idx.push_back(poly[0]);
                        idx.push_back(poly[t]);
                        idx.push_back(poly[t + 1]);
                    }
                }
            }
            if (is_vertex) {
                pos.insert(pos.end(), {static_cast<f32>(row[static_cast<usize>(ix)]),
                                       static_cast<f32>(row[static_cast<usize>(iy)]),
                                       static_cast<f32>(row[static_cast<usize>(iz)])});
                if (has_n) {
                    f32 nv[3] = {static_cast<f32>(row[static_cast<usize>(inx)]),
                                 static_cast<f32>(row[static_cast<usize>(iny)]),
                                 static_cast<f32>(row[static_cast<usize>(inz)])};
                    const f32 len = std::sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
                    if (len > 1e-20f) {
                        nv[0] /= len;
                        nv[1] /= len;
                        nv[2] /= len;
                    }
                    nrm.insert(nrm.end(), {nv[0], nv[1], nv[2]});
                }
                if (has_uv)
                    uv.insert(uv.end(), {static_cast<f32>(row[static_cast<usize>(iu)]),
                                         static_cast<f32>(row[static_cast<usize>(iv)])});
                if (has_c) {
                    for (int c : {ir, ig, ib}) {
                        const double v = row[static_cast<usize>(c)];
                        // Float colors are in [0, 1]; integer colors in [0, 255].
                        const bool is_float = e.props[static_cast<usize>(c)].type == PType::F32 ||
                                              e.props[static_cast<usize>(c)].type == PType::F64;
                        const double s = is_float ? v * 255.0 : v;
                        col.push_back(static_cast<u8>(std::min(255.0, std::max(0.0, s))));
                    }
                    col.push_back(255);
                }
            }
        }
    }
    if (pos.empty() || idx.empty() || !have_face_list) {
        error = "the PLY file has no triangles";
        return false;
    }
    const u32 vcount = static_cast<u32>(pos.size() / 3);
    for (u32 i : idx) {
        if (i >= vcount) {
            error = "PLY face index out of range";
            return false;
        }
    }
    ImpPrim prim;
    prim.geo.pos = std::move(pos);
    prim.geo.idx = std::move(idx);
    if (has_n)
        prim.geo.nrm = std::move(nrm);
    else {
        prim.geo.nrm = smooth_normals(prim.geo.pos, prim.geo.idx);
        warn.push_back("PLY without normals: smooth normals were computed");
    }
    if (has_uv) prim.geo.uv = std::move(uv);
    if (has_c) prim.geo.col = std::move(col);
    scene.meshes.push_back(ImpMesh{"ply", {std::move(prim)}});
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// FBX and OBJ through ufbx

struct UfbxContext {
    const std::string& dir;
    ImpScene& out;
    std::vector<std::string>& warn;
    std::map<const ufbx_mesh*, i32> meshes;
    std::map<const ufbx_material*, i32> materials;
    std::map<const ufbx_texture*, i32> textures; // -1: not usable
    u32 skinned = 0;
    u32 blend_shapes = 0;
};

std::string basename_of(const std::string& p) {
    const usize s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

// The texture bytes: embedded content, or the file next to the source (ufbx resolves the names relative to it).
bool read_texture_bytes(const ufbx_texture* t, const std::string& dir, std::vector<u8>& out) {
    if (t->content.data && t->content.size > 0) {
        const u8* d = static_cast<const u8*>(t->content.data);
        out.assign(d, d + t->content.size);
        return true;
    }
    std::vector<std::string> cands;
    if (t->filename.length)
        cands.push_back(path_join(dir, str_of(std::string_view(t->filename.data, t->filename.length))));
    if (t->relative_filename.length)
        cands.push_back(
            path_join(dir, str_of(std::string_view(t->relative_filename.data, t->relative_filename.length))));
    if (t->absolute_filename.length) {
        const std::string abs = str_of(std::string_view(t->absolute_filename.data, t->absolute_filename.length));
        cands.push_back(abs);
        cands.push_back(path_join(dir, basename_of(abs)));
    }
    for (const std::string& c : cands) {
        if (file_read_all(c.c_str(), out) && !out.empty()) return true;
    }
    return false;
}

i32 convert_texture(UfbxContext& c, const ufbx_texture* t) {
    if (!t) return -1;
    auto it = c.textures.find(t);
    if (it != c.textures.end()) return it->second;
    std::vector<u8> bytes;
    i32 index = -1;
    // Suggested file name: the texture's own file stem (e.g. "wood" for "textures/wood.png").
    std::string name = "texture";
    if (t->filename.length) {
        std::string base = basename_of(str_of(std::string_view(t->filename.data, t->filename.length)));
        const usize dot = base.find_last_of('.');
        if (dot != std::string::npos && dot > 0) base.resize(dot);
        if (!base.empty()) name = base;
    }
    if (read_texture_bytes(t, c.dir, bytes)) {
        if (const char* mime = sniff_mime(bytes)) {
            ImpTexture tex;
            tex.bytes = std::move(bytes);
            tex.mime = mime;
            tex.stem = name;
            index = static_cast<i32>(c.out.textures.size());
            c.out.textures.push_back(std::move(tex));
        }
    }
    if (index < 0) c.warn.push_back("texture '" + name + "' not found or not PNG/JPEG: skipped");
    c.textures[t] = index;
    return index;
}

// Decodes a PNG/JPEG texture to RGBA8 (for the metallic-roughness packing).
bool decode_texture(UfbxContext& c, const ufbx_texture* t, ImageRGBA8& img) {
    std::vector<u8> bytes;
    if (!read_texture_bytes(t, c.dir, bytes) || !sniff_mime(bytes)) return false;
    return image_read_memory(bytes.data(), bytes.size(), img);
}

void png_append(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<u8>*>(context);
    const u8* d = static_cast<const u8*>(data);
    out->insert(out->end(), d, d + size);
}

// glTF metallic-roughness texture from separate roughness and metallic maps (G = roughness, B = metallic). Each map is
// read from its red channel (grayscale); the factors multiply the samples. Returns -1 when neither can be decoded.
i32 pack_metal_rough(UfbxContext& c, const ufbx_texture* rough, const ufbx_texture* metal, f32 rf, f32 mf) {
    ImageRGBA8 a, b;
    const bool ha = rough && decode_texture(c, rough, a);
    const bool hb = metal && decode_texture(c, metal, b);
    if (rough && !ha) c.warn.push_back("roughness map could not be decoded: its factor is used");
    if (metal && !hb) c.warn.push_back("metalness map could not be decoded: its factor is used");
    if (!ha && !hb) return -1;
    const ImageRGBA8& ref = ha ? a : b;
    std::vector<u8> px(static_cast<usize>(ref.width) * ref.height * 4, 255);
    for (u32 y = 0; y < ref.height; ++y) {
        for (u32 x = 0; x < ref.width; ++x) {
            f32 g = clamp01(rf), bl = clamp01(mf);
            if (ha) {
                const u32 sx = x * a.width / ref.width, sy = y * a.height / ref.height;
                g = clamp01(rf * a.pixels[(static_cast<usize>(sy) * a.width + sx) * 4] / 255.0f);
            }
            if (hb) {
                const u32 sx = x * b.width / ref.width, sy = y * b.height / ref.height;
                bl = clamp01(mf * b.pixels[(static_cast<usize>(sy) * b.width + sx) * 4] / 255.0f);
            }
            u8* o = &px[(static_cast<usize>(y) * ref.width + x) * 4];
            o[0] = 255;
            o[1] = static_cast<u8>(std::lround(g * 255.0f));
            o[2] = static_cast<u8>(std::lround(bl * 255.0f));
            o[3] = 255;
        }
    }
    ImpTexture tex;
    tex.mime = "image/png";
    tex.stem = "metal_rough";
    stbi_write_png_to_func(png_append, &tex.bytes, static_cast<int>(ref.width), static_cast<int>(ref.height), 4,
                           px.data(), static_cast<int>(ref.width * 4));
    if (tex.bytes.empty()) return -1;
    const i32 index = static_cast<i32>(c.out.textures.size());
    c.out.textures.push_back(std::move(tex));
    return index;
}

i32 convert_material(UfbxContext& c, const ufbx_material* m) {
    if (!m) return -1;
    auto it = c.materials.find(m);
    if (it != c.materials.end()) return it->second;
    ImpMaterial out;
    out.name = m->name.length ? str_of(std::string_view(m->name.data, m->name.length)) : "material";
    const auto& pbr = m->pbr;
    if (pbr.base_color.has_value) {
        out.base_color[0] = static_cast<f32>(pbr.base_color.value_vec3.x);
        out.base_color[1] = static_cast<f32>(pbr.base_color.value_vec3.y);
        out.base_color[2] = static_cast<f32>(pbr.base_color.value_vec3.z);
    }
    if (pbr.base_factor.has_value) {
        for (int k = 0; k < 3; ++k) out.base_color[k] *= static_cast<f32>(pbr.base_factor.value_real);
    }
    if (pbr.opacity.has_value) out.base_color[3] = clamp01(static_cast<f32>(pbr.opacity.value_real));
    for (f32& v : out.base_color) v = clamp01(v);
    out.base_texture = convert_texture(c, pbr.base_color.texture);

    const ufbx_texture* rt = pbr.roughness.texture;
    const ufbx_texture* mt = pbr.metalness.texture;
    const f32 rf = pbr.roughness.has_value ? clamp01(static_cast<f32>(pbr.roughness.value_real)) : (rt ? 1.0f : 0.6f);
    const f32 mf = pbr.metalness.has_value ? clamp01(static_cast<f32>(pbr.metalness.value_real)) : (mt ? 1.0f : 0.0f);
    if (rt && rt == mt) {
        out.metal_rough_texture = convert_texture(c, rt); // already packed in glTF order
        out.roughness = rf;
        out.metallic = mf;
    } else if (rt || mt) {
        out.metal_rough_texture = pack_metal_rough(c, rt, mt, rf, mf);
        if (out.metal_rough_texture >= 0) {
            out.roughness = 1.0f; // the factors are baked into the packed texture
            out.metallic = 1.0f;
        } else {
            out.roughness = rf;
            out.metallic = mf;
        }
    } else {
        out.roughness = rf;
        out.metallic = mf;
    }

    out.normal_texture = convert_texture(c, pbr.normal_map.texture);

    if (pbr.emission_color.has_value || pbr.emission_color.texture) {
        f32 e[3] = {1.0f, 1.0f, 1.0f};
        if (pbr.emission_color.has_value) {
            e[0] = static_cast<f32>(pbr.emission_color.value_vec3.x);
            e[1] = static_cast<f32>(pbr.emission_color.value_vec3.y);
            e[2] = static_cast<f32>(pbr.emission_color.value_vec3.z);
        }
        const f32 ef = pbr.emission_factor.has_value ? static_cast<f32>(pbr.emission_factor.value_real) : 1.0f;
        for (int k = 0; k < 3; ++k) out.emissive[k] = clamp01(e[k] * ef);
        out.emissive_texture = convert_texture(c, pbr.emission_color.texture);
    }

    const i32 index = static_cast<i32>(c.out.materials.size());
    c.out.materials.push_back(std::move(out));
    c.materials[m] = index;
    return index;
}

i32 convert_mesh(UfbxContext& c, const ufbx_mesh* mesh) {
    auto it = c.meshes.find(mesh);
    if (it != c.meshes.end()) return it->second;
    ImpMesh out;
    out.name = mesh->name.length ? str_of(std::string_view(mesh->name.data, mesh->name.length)) : "mesh";
    if (mesh->skin_deformers.count > 0) ++c.skinned;
    if (mesh->blend_deformers.count > 0) ++c.blend_shapes;

    const bool has_n = mesh->vertex_normal.exists;
    const bool has_uv = mesh->vertex_uv.exists;
    std::vector<u32> tri(mesh->max_face_triangles * 3 + 3);
    std::map<u32, usize> prim_of_slot; // face material slot -> prim index
    // Per primitive: vertex key -> index. The key holds the position index and the normal and UV values (bits), so
    // corners with equal attributes share a vertex even when the source gives each corner its own attribute index.
    std::vector<std::map<std::array<u32, 6>, u32>> seen;

    for (usize f = 0; f < mesh->faces.count; ++f) {
        const ufbx_face face = mesh->faces.data[f];
        if (face.num_indices < 3) continue;
        const u32 slot = f < mesh->face_material.count ? mesh->face_material.data[f] : 0u;
        const u32 ntri = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
        if (ntri == 0) continue;
        auto pit = prim_of_slot.find(slot);
        if (pit == prim_of_slot.end()) {
            pit = prim_of_slot.emplace(slot, out.prims.size()).first;
            out.prims.emplace_back();
            out.prims.back().material =
                slot < mesh->materials.count ? convert_material(c, mesh->materials.data[slot]) : -1;
            seen.emplace_back();
        }
        ImpPrim& prim = out.prims[pit->second];
        auto& dedupe = seen[pit->second];
        for (u32 k = 0; k < ntri * 3; ++k) {
            const u32 corner = tri[k];
            const u32 pi = mesh->vertex_position.indices.data[corner];
            const ufbx_vec3 p = mesh->vertex_position.values.data[pi];
            std::array<u32, 6> key = {pi, 0, 0, 0, 0, 0};
            ufbx_vec3 n{};
            ufbx_vec2 uv{};
            if (has_n) {
                n = mesh->vertex_normal.values.data[mesh->vertex_normal.indices.data[corner]];
                key[1] = bits_of(static_cast<f32>(n.x));
                key[2] = bits_of(static_cast<f32>(n.y));
                key[3] = bits_of(static_cast<f32>(n.z));
            }
            if (has_uv) {
                uv = mesh->vertex_uv.values.data[mesh->vertex_uv.indices.data[corner]];
                key[4] = bits_of(static_cast<f32>(uv.x));
                key[5] = bits_of(static_cast<f32>(uv.y));
            }
            auto vit = dedupe.find(key);
            if (vit == dedupe.end()) {
                const u32 v = prim.geo.vertex_count();
                vit = dedupe.emplace(key, v).first;
                prim.geo.pos.insert(prim.geo.pos.end(),
                                    {static_cast<f32>(p.x), static_cast<f32>(p.y), static_cast<f32>(p.z)});
                if (has_n)
                    prim.geo.nrm.insert(prim.geo.nrm.end(),
                                        {static_cast<f32>(n.x), static_cast<f32>(n.y), static_cast<f32>(n.z)});
                // FBX and OBJ put v = 0 at the bottom; glTF at the top.
                if (has_uv)
                    prim.geo.uv.insert(prim.geo.uv.end(), {static_cast<f32>(uv.x), 1.0f - static_cast<f32>(uv.y)});
            }
            prim.geo.idx.push_back(vit->second);
        }
    }
    out.prims.erase(
        std::remove_if(out.prims.begin(), out.prims.end(), [](const ImpPrim& p) { return p.geo.idx.empty(); }),
        out.prims.end());
    if (!has_n) {
        // ufbx generates normals when the file has none; this is a safety net.
        for (ImpPrim& p : out.prims) p.geo.nrm = smooth_normals(p.geo.pos, p.geo.idx);
    }
    const i32 index = static_cast<i32>(c.out.meshes.size());
    c.out.meshes.push_back(std::move(out));
    c.meshes[mesh] = index;
    return index;
}

u32 emit_node(UfbxContext& c, const ufbx_node* n) {
    ImpNode node;
    node.name = n->name.length ? str_of(std::string_view(n->name.data, n->name.length)) : "";
    const ufbx_matrix& m = n->node_to_parent;
    const f32 col[16] = {static_cast<f32>(m.m00), static_cast<f32>(m.m10), static_cast<f32>(m.m20), 0.0f,
                         static_cast<f32>(m.m01), static_cast<f32>(m.m11), static_cast<f32>(m.m21), 0.0f,
                         static_cast<f32>(m.m02), static_cast<f32>(m.m12), static_cast<f32>(m.m22), 0.0f,
                         static_cast<f32>(m.m03), static_cast<f32>(m.m13), static_cast<f32>(m.m23), 1.0f};
    std::memcpy(node.matrix, col, sizeof(col));
    if (n->mesh) node.mesh = convert_mesh(c, n->mesh);
    std::vector<u32> kids;
    for (usize k = 0; k < n->children.count; ++k) kids.push_back(emit_node(c, n->children.data[k]));
    node.children = std::move(kids);
    const u32 index = static_cast<u32>(c.out.nodes.size());
    c.out.nodes.push_back(std::move(node));
    return index;
}

struct SceneGuard {
    ufbx_scene* s = nullptr;
    ~SceneGuard() {
        if (s) ufbx_free_scene(s);
    }
};

bool load_ufbx(const std::string& path, const std::string& ext, ImpScene& scene, std::string& error,
               ModelImportReport& rep) {
    ufbx_load_opts opts{};
    // glTF / engine axes: +X right, +Y up, +Z front (ufbx's "front" is the opposite of forward). Units in metres.
    opts.target_axes = ufbx_coordinate_axes{UFBX_COORDINATE_AXIS_POSITIVE_X, UFBX_COORDINATE_AXIS_POSITIVE_Y,
                                            UFBX_COORDINATE_AXIS_POSITIVE_Z};
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.generate_missing_normals = true;
    opts.normalize_normals = true;
    opts.load_external_files = true; // OBJ .mtl and its textures
    opts.ignore_missing_external_files = true;
    opts.ignore_animation = true;
    opts.skip_skin_vertices = true;
    opts.skip_mesh_parts = true;
    ufbx_error err{};
    SceneGuard guard;
    guard.s = ufbx_load_file_len(path.c_str(), path.size(), &opts, &err);
    if (!guard.s) {
        char buf[512] = {};
        ufbx_format_error(buf, sizeof(buf), &err);
        error = "cannot read " + ext + " file: " + buf;
        return false;
    }
    const std::string dir = path_parent(path);
    UfbxContext c{dir, scene, rep.warnings, {}, {}, {}, 0, 0};
    const ufbx_scene* s = guard.s;
    if (!s->root_node) {
        error = "the file has no scene";
        return false;
    }
    scene.roots.push_back(emit_node(c, s->root_node));
    rep.skinned_meshes = c.skinned;
    if (c.skinned)
        rep.warnings.push_back(std::to_string(c.skinned) +
                               " skinned mesh(es) written in their bind pose (no skeleton)");
    if (c.blend_shapes) rep.warnings.push_back("blend shapes are not exported (the base mesh is kept)");
    if (scene.meshes.empty()) {
        error = "the file has no meshes";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// glTF 2.0 binary writer

class GltfBuilder {
  public:
    // Appends bytes to the BIN chunk (4-byte aligned) and returns the bufferView index.
    u32 view(const void* data, usize bytes) {
        while (bin_.size() % 4 != 0) bin_.push_back(0);
        const usize off = bin_.size();
        const u8* p = static_cast<const u8*>(data);
        bin_.insert(bin_.end(), p, p + bytes);
        JsonValue v = JsonValue::object();
        v.set("buffer", JsonValue(u32(0)));
        v.set("byteOffset", JsonValue(static_cast<f64>(off)));
        v.set("byteLength", JsonValue(static_cast<f64>(bytes)));
        views_.push(std::move(v));
        return views_count_++;
    }

    u32 accessor(u32 view_index, u32 component_type, u32 count, const char* type, usize components, bool normalized,
                 const f32* mins, const f32* maxs) {
        JsonValue a = JsonValue::object();
        a.set("bufferView", JsonValue(view_index));
        a.set("componentType", JsonValue(component_type));
        a.set("count", JsonValue(count));
        a.set("type", JsonValue(std::string(type)));
        if (normalized) a.set("normalized", JsonValue(true));
        if (mins) {
            JsonValue mn = JsonValue::array(), mx = JsonValue::array();
            for (usize k = 0; k < components; ++k) {
                mn.push(JsonValue(static_cast<f64>(mins[k])));
                mx.push(JsonValue(static_cast<f64>(maxs[k])));
            }
            a.set("min", mn);
            a.set("max", mx);
        }
        accessors_.push(std::move(a));
        return accessors_count_++;
    }

    std::vector<u8>& bin() { return bin_; }
    JsonValue& views() { return views_; }
    JsonValue& accessors() { return accessors_; }
    u32 view_count() const { return views_count_; }
    u32 accessor_count() const { return accessors_count_; }

  private:
    std::vector<u8> bin_;
    JsonValue views_ = JsonValue::array();
    JsonValue accessors_ = JsonValue::array();
    u32 views_count_ = 0;
    u32 accessors_count_ = 0;
};

constexpr u32 kGlTFFloat = 5126, kGlTFUByte = 5121, kGlTFUShort = 5123, kGlTFUInt = 5125;

void put_u32(std::vector<u8>& out, u32 v) {
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<u8>((v >> (8 * k)) & 0xffu));
}

bool write_glb(const ImpScene& scene, const std::string& dst, const ModelImportOptions& options, std::string& error,
               ModelImportReport& rep) {
    GltfBuilder b;
    const std::string dst_dir = path_parent(dst);
    std::string dst_stem = path_stem(dst);
    for (char& ch : dst_stem)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-') ch = '_';

    // Images first: embedded (bufferView) or external files next to the .glb.
    JsonValue images = JsonValue::array();
    for (usize i = 0; i < scene.textures.size(); ++i) {
        const ImpTexture& t = scene.textures[i];
        JsonValue im = JsonValue::object();
        im.set("mimeType", JsonValue(std::string(t.mime)));
        if (options.embed_textures) {
            im.set("bufferView", JsonValue(b.view(t.bytes.data(), t.bytes.size())));
        } else {
            const std::string ext = std::string(t.mime) == "image/png" ? ".png" : ".jpg";
            std::string stem;
            for (char ch : t.stem)
                stem += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') ? ch : '_';
            const std::string name = dst_stem + "_" + (stem.empty() ? "tex" : stem) + std::to_string(i) + ext;
            if (!file_write_all(path_join(dst_dir, name).c_str(), t.bytes.data(), t.bytes.size())) {
                error = "cannot write texture " + name;
                return false;
            }
            im.set("uri", JsonValue(name));
        }
        images.push(std::move(im));
    }
    JsonValue textures = JsonValue::array();
    for (usize i = 0; i < scene.textures.size(); ++i) {
        JsonValue t = JsonValue::object();
        t.set("source", JsonValue(static_cast<u32>(i)));
        textures.push(std::move(t));
    }
    rep.textures = static_cast<u32>(scene.textures.size());

    // Materials.
    JsonValue materials = JsonValue::array();
    for (const ImpMaterial& m : scene.materials) {
        JsonValue pbr = JsonValue::object();
        JsonValue bc = JsonValue::array();
        for (f32 v : m.base_color) bc.push(JsonValue(static_cast<f64>(v)));
        pbr.set("baseColorFactor", bc);
        pbr.set("metallicFactor", JsonValue(static_cast<f64>(m.metallic)));
        pbr.set("roughnessFactor", JsonValue(static_cast<f64>(m.roughness)));
        if (m.base_texture >= 0) {
            JsonValue ti = JsonValue::object();
            ti.set("index", JsonValue(static_cast<u32>(m.base_texture)));
            pbr.set("baseColorTexture", ti);
        }
        if (m.metal_rough_texture >= 0) {
            JsonValue ti = JsonValue::object();
            ti.set("index", JsonValue(static_cast<u32>(m.metal_rough_texture)));
            pbr.set("metallicRoughnessTexture", ti);
        }
        JsonValue jm = JsonValue::object();
        if (!m.name.empty()) jm.set("name", JsonValue(m.name));
        jm.set("pbrMetallicRoughness", pbr);
        if (m.normal_texture >= 0) {
            JsonValue ti = JsonValue::object();
            ti.set("index", JsonValue(static_cast<u32>(m.normal_texture)));
            jm.set("normalTexture", ti);
        }
        if (m.emissive[0] > 0.0f || m.emissive[1] > 0.0f || m.emissive[2] > 0.0f) {
            JsonValue ef = JsonValue::array();
            for (f32 v : m.emissive) ef.push(JsonValue(static_cast<f64>(v)));
            jm.set("emissiveFactor", ef);
        }
        if (m.emissive_texture >= 0) {
            JsonValue ti = JsonValue::object();
            ti.set("index", JsonValue(static_cast<u32>(m.emissive_texture)));
            jm.set("emissiveTexture", ti);
        }
        if (m.base_color[3] < 1.0f) jm.set("alphaMode", JsonValue(std::string("BLEND")));
        materials.push(std::move(jm));
    }
    rep.materials = static_cast<u32>(scene.materials.size());

    // Meshes: one glTF mesh per source mesh that has triangles; meshes_map maps source index -> glTF index or -1.
    JsonValue meshes = JsonValue::array();
    std::vector<i32> mesh_map(scene.meshes.size(), -1);
    for (usize mi = 0; mi < scene.meshes.size(); ++mi) {
        const ImpMesh& m = scene.meshes[mi];
        JsonValue prims = JsonValue::array();
        for (const ImpPrim& ip : m.prims) {
            const Prim& g = ip.geo;
            const u32 vc = g.vertex_count();
            if (g.idx.size() < 3 || vc == 0) continue;
            JsonValue attrs = JsonValue::object();
            {
                f32 mn[3] = {g.pos[0], g.pos[1], g.pos[2]}, mx[3] = {g.pos[0], g.pos[1], g.pos[2]};
                for (u32 v = 0; v < vc; ++v)
                    for (int k = 0; k < 3; ++k) {
                        mn[k] = std::min(mn[k], g.pos[v * 3 + static_cast<u32>(k)]);
                        mx[k] = std::max(mx[k], g.pos[v * 3 + static_cast<u32>(k)]);
                    }
                const u32 vb = b.view(g.pos.data(), g.pos.size() * sizeof(f32));
                attrs.set("POSITION", JsonValue(b.accessor(vb, kGlTFFloat, vc, "VEC3", 3, false, mn, mx)));
            }
            if (!g.nrm.empty()) {
                const u32 nb = b.view(g.nrm.data(), g.nrm.size() * sizeof(f32));
                attrs.set("NORMAL", JsonValue(b.accessor(nb, kGlTFFloat, vc, "VEC3", 3, false, nullptr, nullptr)));
            }
            if (!g.uv.empty()) {
                const u32 ub = b.view(g.uv.data(), g.uv.size() * sizeof(f32));
                attrs.set("TEXCOORD_0", JsonValue(b.accessor(ub, kGlTFFloat, vc, "VEC2", 2, false, nullptr, nullptr)));
            }
            if (!g.tan.empty()) {
                const u32 tb = b.view(g.tan.data(), g.tan.size() * sizeof(f32));
                attrs.set("TANGENT", JsonValue(b.accessor(tb, kGlTFFloat, vc, "VEC4", 4, false, nullptr, nullptr)));
            }
            if (!g.col.empty()) {
                const u32 cb = b.view(g.col.data(), g.col.size());
                attrs.set("COLOR_0", JsonValue(b.accessor(cb, kGlTFUByte, vc, "VEC4", 4, true, nullptr, nullptr)));
            }
            const bool wide = vc > 65535;
            std::vector<u8> ib;
            if (wide) {
                ib.resize(g.idx.size() * 4);
                std::memcpy(ib.data(), g.idx.data(), ib.size());
            } else {
                ib.resize(g.idx.size() * 2);
                for (usize k = 0; k < g.idx.size(); ++k) {
                    const u16 v = static_cast<u16>(g.idx[k]);
                    std::memcpy(&ib[k * 2], &v, 2);
                }
            }
            const u32 ibv = b.view(ib.data(), ib.size());
            JsonValue prim = JsonValue::object();
            prim.set("attributes", attrs);
            prim.set("indices",
                     JsonValue(b.accessor(ibv, wide ? kGlTFUInt : kGlTFUShort, static_cast<u32>(g.idx.size()), "SCALAR",
                                          1, false, nullptr, nullptr)));
            if (ip.material >= 0) prim.set("material", JsonValue(static_cast<u32>(ip.material)));
            prims.push(std::move(prim));
            ++rep.primitives;
            rep.triangles_out += g.idx.size() / 3;
        }
        if (prims.size() == 0) continue;
        JsonValue jmesh = JsonValue::object();
        if (!m.name.empty()) jmesh.set("name", JsonValue(m.name));
        jmesh.set("primitives", prims);
        mesh_map[mi] = static_cast<i32>(meshes.size());
        meshes.push(std::move(jmesh));
    }
    rep.meshes = static_cast<u32>(meshes.size());
    if (rep.primitives == 0) {
        error = "no triangles to write";
        return false;
    }

    // Nodes: the scene's nodes, then a root node for the source-space matrix when it is not the identity.
    JsonValue nodes = JsonValue::array();
    for (const ImpNode& n : scene.nodes) {
        JsonValue jn = JsonValue::object();
        if (!n.name.empty()) jn.set("name", JsonValue(n.name));
        if (!is_identity(n.matrix)) {
            JsonValue mat = JsonValue::array();
            for (f32 v : n.matrix) mat.push(JsonValue(static_cast<f64>(v)));
            jn.set("matrix", mat);
        }
        if (n.mesh >= 0 && mesh_map[static_cast<usize>(n.mesh)] >= 0)
            jn.set("mesh", JsonValue(static_cast<u32>(mesh_map[static_cast<usize>(n.mesh)])));
        if (!n.children.empty()) {
            JsonValue ch = JsonValue::array();
            for (u32 k : n.children) ch.push(JsonValue(k));
            jn.set("children", ch);
        }
        nodes.push(std::move(jn));
    }
    rep.nodes = static_cast<u32>(nodes.size());
    JsonValue top = JsonValue::array();
    if (!is_identity(scene.root_matrix)) {
        JsonValue jn = JsonValue::object();
        JsonValue mat = JsonValue::array();
        for (f32 v : scene.root_matrix) mat.push(JsonValue(static_cast<f64>(v)));
        jn.set("matrix", mat);
        JsonValue ch = JsonValue::array();
        for (u32 r : scene.roots) ch.push(JsonValue(r));
        jn.set("children", ch);
        top.push(JsonValue(static_cast<u32>(nodes.size())));
        nodes.push(std::move(jn));
        rep.nodes = static_cast<u32>(nodes.size());
    } else {
        for (u32 r : scene.roots) top.push(JsonValue(r));
    }

    JsonValue root = JsonValue::object();
    JsonValue asset = JsonValue::object();
    asset.set("version", JsonValue(std::string("2.0")));
    asset.set("generator", JsonValue(std::string("Tefra model import")));
    root.set("asset", asset);
    root.set("scene", JsonValue(u32(0)));
    JsonValue scenes = JsonValue::array();
    JsonValue s0 = JsonValue::object();
    s0.set("nodes", top);
    scenes.push(std::move(s0));
    root.set("scenes", scenes);
    root.set("nodes", nodes);
    if (meshes.size() > 0) root.set("meshes", meshes);
    if (materials.size() > 0) root.set("materials", materials);
    if (textures.size() > 0) {
        root.set("textures", textures);
        root.set("images", images);
    }
    if (b.accessor_count() > 0) root.set("accessors", b.accessors());
    if (b.view_count() > 0) root.set("bufferViews", b.views());
    if (!b.bin().empty()) {
        JsonValue buf = JsonValue::object();
        buf.set("byteLength", JsonValue(static_cast<f64>(b.bin().size())));
        JsonValue bufs = JsonValue::array();
        bufs.push(std::move(buf));
        root.set("buffers", bufs);
    }

    std::string json = root.dump(false);
    while (json.size() % 4 != 0) json.push_back(' ');
    std::vector<u8>& bin = b.bin();
    while (bin.size() % 4 != 0) bin.push_back(0);

    std::vector<u8> out;
    out.reserve(12 + 8 + json.size() + (bin.empty() ? 0 : 8 + bin.size()));
    put_u32(out, 0x46546c67u); // "glTF"
    put_u32(out, 2u);
    put_u32(out, 0u); // total length, patched below
    put_u32(out, static_cast<u32>(json.size()));
    put_u32(out, 0x4e4f534au); // "JSON"
    out.insert(out.end(), json.begin(), json.end());
    if (!bin.empty()) {
        put_u32(out, static_cast<u32>(bin.size()));
        put_u32(out, 0x004e4942u); // "BIN\0"
        out.insert(out.end(), bin.begin(), bin.end());
    }
    const u32 total = static_cast<u32>(out.size());
    for (int k = 0; k < 4; ++k) out[8 + static_cast<usize>(k)] = static_cast<u8>((total >> (8 * k)) & 0xffu);

    if (!dst_dir.empty() && !make_dirs(dst_dir.c_str())) {
        error = "cannot create " + dst_dir;
        return false;
    }
    if (!file_write_all(dst.c_str(), out.data(), out.size())) {
        error = "cannot write " + dst;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Post-processing: simplification, tangents, source space

void finalize_prim(Prim& p, const ModelImportOptions& o, ModelImportReport& rep) {
    rep.triangles_in += p.idx.size() / 3;
    if (p.idx.size() < 3) {
        p.idx.clear();
        return;
    }
    if (o.max_triangles > 0 && p.idx.size() / 3 > o.max_triangles) {
        std::vector<u32> dst(p.idx.size());
        float err = 0.0f;
        const size_t n = meshopt_simplify(dst.data(), p.idx.data(), p.idx.size(), p.pos.data(), p.vertex_count(),
                                          3 * sizeof(f32), static_cast<size_t>(o.max_triangles) * 3, 1.0f, 0, &err);
        dst.resize(n);
        p.idx = std::move(dst);
    }
    compact(p);
    if (p.idx.size() >= 3 && o.write_tangents && !p.nrm.empty() && !p.uv.empty()) p.tan = tangents_from_uv(p);
}

// Source space for formats without axes or units: the scale of unit_scale, and Z-up files turned to +Y up (x, y, z)
// -> (x, z, -y), a right-handed rotation. Applied above the roots, so the nodes keep their own transforms.
void set_source_space(ImpScene& s, const ModelImportOptions& o) {
    set_identity(s.root_matrix);
    if (s.format == "fbx") return; // FBX carries its units and axes: ufbx converted them already
    const f32 k = o.unit_scale > 0.0f && std::isfinite(o.unit_scale) ? o.unit_scale : 1.0f;
    const bool z_up = o.source_up == ModelUpAxis::Z || (o.source_up == ModelUpAxis::Auto && s.format != "obj");
    if (z_up) {
        // Columns: image of X, Y, Z. X -> X, Y -> -Z, Z -> Y; scaled by k.
        const f32 m[16] = {k, 0, 0, 0, 0, 0, -k, 0, 0, k, 0, 0, 0, 0, 0, 1};
        std::memcpy(s.root_matrix, m, sizeof(m));
    } else {
        const f32 m[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, 0, 0, 0, 1};
        std::memcpy(s.root_matrix, m, sizeof(m));
    }
}

} // namespace

std::string model_import_format(const std::string& path) {
    const std::string ext = lower(path_extension(path));
    if (ext == ".fbx") return "fbx";
    if (ext == ".obj") return "obj";
    if (ext == ".stl") return "stl";
    if (ext == ".ply") return "ply";
    return "";
}

bool convert_to_glb(const char* src, const char* dst_glb, const ModelImportOptions& options, std::string* error,
                    ModelImportReport* report) {
    ModelImportReport local;
    ModelImportReport& rep = report ? *report : local;
    rep = ModelImportReport{};
    std::string err;
    const std::string path = src ? src : "";
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    const std::string fmt = model_import_format(path);
    if (fmt.empty()) return fail("unsupported model format (expected .fbx, .obj, .stl or .ply): " + path);
    if (!dst_glb || !*dst_glb) return fail("no output path");
    rep.format = fmt;

    ImpScene scene;
    scene.format = fmt;
    bool ok = false;
    if (fmt == "stl" || fmt == "ply") {
        std::vector<u8> data;
        if (!file_read_all(path.c_str(), data) || data.empty()) return fail("cannot read " + path);
        ok = fmt == "stl" ? parse_stl(data, scene, err) : parse_ply(data, scene, err, rep.warnings);
        if (ok) {
            // One node holding the mesh; the source space goes above it.
            ImpNode n;
            set_identity(n.matrix);
            n.mesh = 0;
            scene.nodes.push_back(n);
            scene.roots.push_back(0);
        }
    } else {
        ok = load_ufbx(path, fmt, scene, err, rep);
    }
    if (!ok) return fail(err);

    set_source_space(scene, options);
    for (ImpMesh& m : scene.meshes) {
        for (ImpPrim& p : m.prims) finalize_prim(p.geo, options, rep);
        m.prims.erase(
            std::remove_if(m.prims.begin(), m.prims.end(), [](const ImpPrim& p) { return p.geo.idx.size() < 3; }),
            m.prims.end());
    }
    if (!write_glb(scene, dst_glb, options, err, rep)) return fail(err);
    return true;
}

} // namespace tefra::render
