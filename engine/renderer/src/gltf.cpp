#include "tefra/renderer/gltf.h"

#include "tefra/renderer/gltf_skeleton.h"
#include "tefra/renderer/scene.h"
#include <meshoptimizer.h>

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/jobs.h"
#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/rhi/upload.h"

#include <cgltf.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tefra::render {

namespace {

// Bump when the meshing changes (simplification, tangents, vertex packing): stale cache files are then ignored.
constexpr u32 kGltfCacheVersion = 1;
constexpr char kGltfCacheMagic[4] = {'T', 'G', 'L', 'C'};

} // namespace

// CPU result of one triangle primitive (gltf_prepare). Only valid primitives carry geometry.
struct GltfPrimitive {
    bool valid = false;
    bool skinned = false; // skinned meshes are never simplified and never cached
    MeshData mesh;
    bool has_lod1 = false;
    std::vector<u32> lod1_indices; // coarser LOD over mesh.vertices
};

class GltfPrepared {
  public:
    GltfPrepared(std::string p, const GltfLoadOptions& o) : path(std::move(p)), options(o) {}
    ~GltfPrepared() { cgltf_free(data); }
    GltfPrepared(const GltfPrepared&) = delete;
    GltfPrepared& operator=(const GltfPrepared&) = delete;

    std::string path;
    GltfLoadOptions options;
    cgltf_data* data = nullptr;
    // Every triangle primitive of the file; the index is its cache ordinal.
    std::vector<const cgltf_primitive*> primitives;
    std::map<const cgltf_primitive*, u32> ordinal;
    std::vector<GltfPrimitive> meshes;
};

namespace {

template <typename T, usize N> bool read_attr(const cgltf_accessor* acc, std::vector<T>& out) {
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i)
        if (!cgltf_accessor_read_float(acc, i, &out[i][0], N)) return false;
    return true;
}

// Drops the vertices no index refers to any more (a simplified LOD keeps the whole source vertex array): the rendered
// surface is unchanged, the vertex buffer and the cache shrink. extra holds further index lists over the same vertices
// (the coarser LOD), renumbered together.
void compact_mesh(MeshData& md, std::vector<u32>& extra) {
    constexpr u32 kUnused = 0xFFFFFFFFu;
    std::vector<u32> remap(md.vertices.size(), kUnused);
    for (u32 i : md.indices) remap[i] = 0;
    for (u32 i : extra) remap[i] = 0;
    std::vector<Vertex> kept;
    kept.reserve(md.vertices.size());
    for (usize v = 0; v < md.vertices.size(); ++v)
        if (remap[v] != kUnused) {
            remap[v] = static_cast<u32>(kept.size());
            kept.push_back(md.vertices[v]);
        }
    md.vertices.swap(kept);
    for (u32& i : md.indices) i = remap[i];
    for (u32& i : extra) i = remap[i];
}

// Decodes one primitive (no GPU work): attributes, indices, normals and tangents, skinning weights, and for static
// meshes the simplification requested by the options.
void build_primitive(const cgltf_primitive& prim, const GltfLoadOptions& options, GltfPrimitive& out) {
    const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tan = nullptr, *uv = nullptr;
    const cgltf_accessor *joints = nullptr, *weights = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        const cgltf_attribute& a = prim.attributes[i];
        if (a.type == cgltf_attribute_type_position)
            pos = a.data;
        else if (a.type == cgltf_attribute_type_normal)
            nrm = a.data;
        else if (a.type == cgltf_attribute_type_tangent)
            tan = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
            uv = a.data;
        else if (a.type == cgltf_attribute_type_joints && a.index == 0)
            joints = a.data;
        else if (a.type == cgltf_attribute_type_weights && a.index == 0)
            weights = a.data;
    }
    if (!pos) return;
    struct F3 {
        f32 v[3];
        f32& operator[](int i) { return v[i]; }
    };
    struct F4 {
        f32 v[4];
        f32& operator[](int i) { return v[i]; }
    };
    struct F2 {
        f32 v[2];
        f32& operator[](int i) { return v[i]; }
    };
    std::vector<F3> p, n;
    std::vector<F4> t;
    std::vector<F2> u;
    read_attr<F3, 3>(pos, p);
    if (nrm) read_attr<F3, 3>(nrm, n);
    if (tan) read_attr<F4, 4>(tan, t);
    if (uv) read_attr<F2, 2>(uv, u);

    MeshData& md = out.mesh;
    md.vertices.resize(p.size());
    for (usize i = 0; i < p.size(); ++i) {
        const vec3 position{p[i][0], p[i][1], p[i][2]};
        const vec3 normal = nrm ? vec3{n[i][0], n[i][1], n[i][2]} : vec3{0, 1, 0};
        const vec2 texcoord = uv ? vec2{u[i][0], u[i][1]} : vec2{};
        const vec3 tangent = tan ? vec3{t[i][0], t[i][1], t[i][2]} : vec3{1, 0, 0};
        const f32 sign = tan ? (t[i][3] < 0.0f ? -1.0f : 1.0f) : 1.0f;
        md.vertices[i] = make_vertex(position, normal, texcoord, tangent, sign);
    }
    if (prim.indices) {
        md.indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            md.indices[i] = static_cast<u32>(cgltf_accessor_read_index(prim.indices, i));
    } else {
        md.indices.resize(p.size());
        for (u32 i = 0; i < md.indices.size(); ++i) md.indices[i] = i;
    }
    if (!nrm) {
        // Flat-ish normals from faces.
        std::vector<vec3> acc(md.vertices.size());
        for (usize i = 0; i + 2 < md.indices.size(); i += 3) {
            const Vertex &a = md.vertices[md.indices[i]], &b = md.vertices[md.indices[i + 1]],
                         &c = md.vertices[md.indices[i + 2]];
            const vec3 fn = cross(vec3{b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]},
                                  vec3{c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]});
            for (int k = 0; k < 3; ++k) acc[md.indices[i + usize(k)]] += fn;
        }
        for (usize i = 0; i < md.vertices.size(); ++i) md.vertices[i].normal = oct_encode(normalize(acc[i]));
    }
    if (!tan) compute_tangents(md);
    if (joints && weights && joints->count == md.vertices.size() && weights->count == md.vertices.size()) {
        md.skin.resize(md.vertices.size());
        for (usize i = 0; i < md.vertices.size(); ++i) {
            f32 w[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cgltf_uint j[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(joints, i, j, 4);
            cgltf_accessor_read_float(weights, i, w, 4);
            const f32 sum = w[0] + w[1] + w[2] + w[3];
            for (int k = 0; k < 4; ++k) {
                md.skin[i].joints[k] = static_cast<u32>(j[k]);
                md.skin[i].weights[k] = sum > 1e-6f ? w[k] / sum : (k == 0 ? 1.0f : 0.0f);
            }
        }
    }
    out.valid = true;
    out.skinned = !md.skin.empty();
    // Simplification (static meshes only): keeps the borders of UV seams, error target 2% of the extent.
    auto simplify = [&](u32 max_tris) {
        std::vector<u32> o(md.indices.size());
        const usize got = meshopt_simplify(o.data(), md.indices.data(), md.indices.size(), &md.vertices[0].pos[0],
                                           md.vertices.size(), sizeof(Vertex), usize(max_tris) * 3, 0.05f, 0, nullptr);
        o.resize(got);
        return o;
    };
    bool simplified = false;
    if (!out.skinned && options.lod1_triangles && md.indices.size() / 3 > options.lod1_triangles) {
        out.lod1_indices = simplify(options.lod1_triangles);
        out.has_lod1 = !out.lod1_indices.empty();
        simplified = true;
    }
    if (!out.skinned && options.max_triangles && md.indices.size() / 3 > options.max_triangles) {
        const usize before = md.indices.size() / 3;
        md.indices = simplify(options.max_triangles);
        simplified = true;
        TEFRA_LOG_INFO("glTF: simplified a primitive %zu -> %zu triangles", before, md.indices.size() / 3);
    }
    if (simplified) compact_mesh(md, out.lod1_indices);
}

// ---- Mesh cache
// ------------------------------------------------------------------------------------------------------

u64 fnv1a(const std::string& s) {
    u64 h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// Identifies the simplified meshes: the file (path, size, mtime) and everything that shapes them.
std::string cache_key(const std::string& path, const GltfLoadOptions& o) {
    return path + "|" + std::to_string(file_size(path)) + "|" + std::to_string(file_mtime(path)) + "|" +
           std::to_string(o.max_triangles) + "|" + std::to_string(o.lod1_triangles) + "|" +
           std::to_string(kGltfCacheVersion) + "|" + std::to_string(sizeof(Vertex));
}

// One file per key: config_dir()/cache/gltf/<stem>-<hash of key>.tglc. Empty when TEFRA_NO_GLTF_CACHE=1.
std::string cache_file(const std::string& path, const std::string& key) {
    if (const char* off = std::getenv("TEFRA_NO_GLTF_CACHE"); off && off[0] == '1') return {};
    char name[40];
    std::snprintf(name, sizeof name, "-%016llx.tglc", static_cast<unsigned long long>(fnv1a(key)));
    return path_join(path_join(config_dir(), "cache/gltf"), path_stem(path) + name);
}

void put_u32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}

void put_bytes(std::vector<u8>& out, const void* p, usize n) {
    const u8* b = static_cast<const u8*>(p);
    out.insert(out.end(), b, b + n);
}

u32 get_u32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

// File layout: magic, version, vertex size, key length + key, primitive count; then per static primitive: ordinal,
// flags (bit 0 = LOD1), vertex count, index count, LOD1 index count, and the raw vertices, indices and LOD1 indices.
// The whole file is parsed before anything is taken: a bad file leaves prims untouched.
bool read_cache(const std::string& file, const std::string& key, std::vector<GltfPrimitive>& prims) {
    std::vector<u8> bytes;
    if (file.empty() || !file_read_all(file.c_str(), bytes) || bytes.size() < 20) return false;
    const u8* p = bytes.data();
    const u8* end = p + bytes.size();
    if (std::memcmp(p, kGltfCacheMagic, 4) != 0 || get_u32(p + 4) != kGltfCacheVersion ||
        get_u32(p + 8) != sizeof(Vertex))
        return false;
    const u32 key_len = get_u32(p + 12);
    p += 16;
    if (key.size() != key_len || usize(end - p) < usize(key_len) + 4 || std::memcmp(p, key.data(), key_len) != 0)
        return false;
    p += key_len;
    const u32 count = get_u32(p);
    p += 4;
    std::vector<GltfPrimitive> loaded(prims.size());
    for (u32 e = 0; e < count; ++e) {
        if (end - p < 20) return false;
        const u32 ordinal = get_u32(p), flags = get_u32(p + 4), nv = get_u32(p + 8), ni = get_u32(p + 12),
                  nl = get_u32(p + 16);
        p += 20;
        const usize need = usize(nv) * sizeof(Vertex) + usize(ni) * 4 + usize(nl) * 4;
        if (usize(end - p) < need || ordinal >= prims.size()) return false;
        GltfPrimitive& pm = loaded[ordinal];
        pm.valid = true;
        pm.mesh.vertices.resize(nv);
        std::memcpy(pm.mesh.vertices.data(), p, usize(nv) * sizeof(Vertex));
        p += usize(nv) * sizeof(Vertex);
        pm.mesh.indices.resize(ni);
        std::memcpy(pm.mesh.indices.data(), p, usize(ni) * 4);
        p += usize(ni) * 4;
        pm.has_lod1 = (flags & 1u) != 0;
        pm.lod1_indices.resize(nl);
        if (nl) std::memcpy(pm.lod1_indices.data(), p, usize(nl) * 4);
        p += usize(nl) * 4;
    }
    for (usize i = 0; i < prims.size(); ++i)
        if (loaded[i].valid) prims[i] = std::move(loaded[i]);
    return true;
}

void write_cache(const std::string& file, const std::string& key, const std::vector<GltfPrimitive>& prims) {
    if (file.empty()) return;
    std::vector<u8> out;
    put_bytes(out, kGltfCacheMagic, 4);
    put_u32(out, kGltfCacheVersion);
    put_u32(out, sizeof(Vertex));
    put_u32(out, static_cast<u32>(key.size()));
    put_bytes(out, key.data(), key.size());
    u32 count = 0;
    for (const GltfPrimitive& pm : prims) count += (pm.valid && !pm.skinned) ? 1u : 0u;
    put_u32(out, count);
    for (usize i = 0; i < prims.size(); ++i) {
        const GltfPrimitive& pm = prims[i];
        if (!pm.valid || pm.skinned) continue;
        put_u32(out, static_cast<u32>(i));
        put_u32(out, pm.has_lod1 ? 1u : 0u);
        put_u32(out, static_cast<u32>(pm.mesh.vertices.size()));
        put_u32(out, static_cast<u32>(pm.mesh.indices.size()));
        put_u32(out, static_cast<u32>(pm.lod1_indices.size()));
        put_bytes(out, pm.mesh.vertices.data(), pm.mesh.vertices.size() * sizeof(Vertex));
        put_bytes(out, pm.mesh.indices.data(), pm.mesh.indices.size() * 4);
        put_bytes(out, pm.lod1_indices.data(), pm.lod1_indices.size() * 4);
    }
    make_dirs(path_parent(file).c_str());
    // Unique temporary name: two threads may prepare the same file at once; rename() keeps the result whole.
    static std::atomic<u32> writes{0};
    const std::string tmp = file + ".tmp" + std::to_string(writes.fetch_add(1));
    if (file_write_all(tmp.c_str(), out.data(), out.size())) {
        rename_path(tmp, file);
        keep_newest_files(path_parent(file), ".tglc", 64);
    }
}

struct Loader {
    Scene& scene;
    const rhi::Device& dev;
    std::string base_dir;
    GltfPrepared& prep;
    cgltf_data* data = nullptr;
    mat4
        placement{}; // asset placement (GltfLoadOptions): where skinned meshes go, joints already carry their ancestors
    std::map<std::pair<const cgltf_image*, bool>, u32> texture_cache; // (image, srgb) -> bindless
    std::map<const cgltf_material*, u32> material_cache;
    std::map<const cgltf_primitive*, u32> mesh_cache;
    std::map<const cgltf_skin*, u32> skin_cache; // scene skin index per glTF skin
    GltfLoadResult result;

    Loader(Scene& s, GltfPrepared& p)
        : scene(s), dev(s.device()), base_dir(path_parent(p.path)), prep(p), data(p.data) {}

    bool decode_image(const cgltf_image* img, ImageRGBA8& out) {
        if (img->buffer_view) {
            const cgltf_buffer_view* bv = img->buffer_view;
            const u8* bytes = static_cast<const u8*>(bv->buffer->data) + bv->offset;
            return image_read_memory(bytes, bv->size, out);
        }
        if (img->uri) {
            std::string uri = img->uri;
            cgltf_decode_uri(uri.data());
            uri.resize(std::char_traits<char>::length(uri.c_str()));
            return image_read(path_join(base_dir, uri).c_str(), out);
        }
        return false;
    }

    u32 texture(const cgltf_texture_view& view, bool srgb, u32 fallback) {
        if (!view.texture || !view.texture->image) return fallback;
        const cgltf_image* img = view.texture->image;
        const auto key = std::make_pair(img, srgb);
        if (auto it = texture_cache.find(key); it != texture_cache.end()) return it->second;
        ImageRGBA8 pixels;
        if (!decode_image(img, pixels)) {
            TEFRA_LOG_WARN("glTF: cannot decode image '%s'", img->uri ? img->uri : "(embedded)");
            texture_cache[key] = fallback;
            return fallback;
        }
        rhi::TextureUpload up;
        up.pixels = pixels.pixels.data();
        up.width = pixels.width;
        up.height = pixels.height;
        up.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        up.name = img->name ? img->name : "gltf_texture";
        const u32 index = scene.add_texture(rhi::upload_texture(dev, up));
        texture_cache[key] = index;
        ++result.textures;
        return index;
    }

    // glTF keeps occlusion (R) and metallic-roughness (G, B) in separate textures; Tefra uses one ORM texture.
    u32 orm_texture(const cgltf_material& m) {
        const cgltf_texture_view& mr = m.pbr_metallic_roughness.metallic_roughness_texture;
        const cgltf_texture_view& occ = m.occlusion_texture;
        const bool has_mr = mr.texture && mr.texture->image;
        const bool has_occ = occ.texture && occ.texture->image;
        if (!has_occ || (has_mr && occ.texture->image == mr.texture->image))
            return texture(mr, false, scene.white_texture());
        ImageRGBA8 orm, occ_px;
        if (!decode_image(occ.texture->image, occ_px)) return texture(mr, false, scene.white_texture());
        if (has_mr && decode_image(mr.texture->image, orm) && orm.width == occ_px.width &&
            orm.height == occ_px.height) {
            for (usize i = 0; i < orm.pixels.size(); i += 4) orm.pixels[i] = occ_px.pixels[i];
        } else {
            orm = occ_px;
            for (usize i = 0; i < orm.pixels.size(); i += 4) {
                orm.pixels[i + 1] = 255; // roughness factor applies
                orm.pixels[i + 2] = 255; // metallic factor applies
            }
        }
        rhi::TextureUpload up;
        up.pixels = orm.pixels.data();
        up.width = orm.width;
        up.height = orm.height;
        up.format = VK_FORMAT_R8G8B8A8_UNORM;
        up.name = "gltf_orm";
        ++result.textures;
        return scene.add_texture(rhi::upload_texture(dev, up));
    }

    u32 material(const cgltf_material* m) {
        if (!m) {
            static const cgltf_material* kDefault = nullptr;
            if (auto it = material_cache.find(kDefault); it != material_cache.end()) return it->second;
            MaterialGPU g{};
            g.base_color = {0.8f, 0.8f, 0.8f, 1.0f};
            g.roughness = 0.7f;
            g.base_color_tex = scene.white_texture();
            g.normal_tex = scene.flat_normal_texture();
            g.orm_tex = scene.white_texture();
            g.emissive_tex = kNoTexture;
            return material_cache[kDefault] = scene.add_material(g);
        }
        if (auto it = material_cache.find(m); it != material_cache.end()) return it->second;
        MaterialGPU g{};
        const auto& pbr = m->pbr_metallic_roughness;
        g.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                        pbr.base_color_factor[3]};
        g.roughness = pbr.roughness_factor;
        g.metallic = pbr.metallic_factor;
        g.base_color_tex = texture(pbr.base_color_texture, true, scene.white_texture());
        g.normal_tex = texture(m->normal_texture, false, scene.flat_normal_texture());
        g.normal_scale = m->normal_texture.texture ? m->normal_texture.scale : 1.0f;
        g.orm_tex = orm_texture(*m);
        g.occlusion_strength = m->occlusion_texture.texture ? m->occlusion_texture.scale : 0.0f;
        g.emissive_tex = m->emissive_texture.texture ? texture(m->emissive_texture, true, kNoTexture) : kNoTexture;
        f32 emissive_strength = m->has_emissive_strength ? m->emissive_strength.emissive_strength : 1.0f;
        g.emissive_cutoff = {m->emissive_factor[0] * emissive_strength, m->emissive_factor[1] * emissive_strength,
                             m->emissive_factor[2] * emissive_strength, 0.0f};
        if (m->alpha_mode == cgltf_alpha_mode_mask) g.emissive_cutoff.w = m->alpha_cutoff;
        if (m->alpha_mode == cgltf_alpha_mode_blend) g.emissive_cutoff.w = 0.5f; // M1: blend approximated by mask
        if (m->double_sided) g.flags |= 2u;
        ++result.materials;
        return material_cache[m] = scene.add_material(g);
    }

    // Scene skin of a glTF skin: palette size = joint count, starting at the rest pose.
    u32 skin(const cgltf_skin* sk) {
        if (auto it = skin_cache.find(sk); it != skin_cache.end()) return it->second;
        GltfSkeleton skel;
        if (!read_gltf_skin(data, sk, skel)) return kNoSkin;
        const u32 index = scene.add_skin(static_cast<u32>(skel.joints.size()));
        scene.set_skin_palette(index, rest_palette(skel));
        ++result.skins;
        return skin_cache[sk] = index;
    }

    // Uploads a prepared primitive (main thread): its mesh, and the coarser LOD when the options asked for one.
    u32 mesh(const cgltf_primitive& prim) {
        if (auto it = mesh_cache.find(&prim); it != mesh_cache.end()) return it->second;
        const auto oit = prep.ordinal.find(&prim);
        if (oit == prep.ordinal.end()) return ~0u;
        const GltfPrimitive& pm = prep.meshes[oit->second];
        if (!pm.valid) return ~0u;
        const MeshData& md = pm.mesh;
        result.triangles += md.indices.size() / 3;
        ++result.meshes;
        const u32 id = scene.add_mesh(md);
        if (!pm.lod1_indices.empty()) {
            MeshData coarse;
            coarse.vertices = md.vertices;
            coarse.indices = pm.lod1_indices;
            result.lod1.push_back({id, scene.add_mesh(coarse)});
        }
        return mesh_cache[&prim] = id;
    }

    void node(const cgltf_node* nd, const mat4& world_from_asset) {
        f32 local[16];
        cgltf_node_transform_local(nd, local);
        mat4 l;
        for (int c = 0; c < 4; ++c) l.c[c] = {local[c * 4 + 0], local[c * 4 + 1], local[c * 4 + 2], local[c * 4 + 3]};
        // glTF: the transform of a skinned mesh node is ignored; its vertices are in the skeleton's space.
        const mat4 world = nd->skin ? placement : world_from_asset * l;
        if (nd->mesh) {
            // Decompose TRS (assumes no shear, as glTF node transforms are TRS).
            const vec3 t = world.c[3].xyz();
            const vec3 sx = world.c[0].xyz(), sy = world.c[1].xyz(), sz = world.c[2].xyz();
            const vec3 s{length(sx), length(sy), length(sz)};
            mat4 r;
            r.c[0] = vec4(sx / s.x, 0);
            r.c[1] = vec4(sy / s.y, 0);
            r.c[2] = vec4(sz / s.z, 0);
            // Rotation matrix -> quaternion.
            const f32 tr = r.at(0, 0) + r.at(1, 1) + r.at(2, 2);
            quat q;
            if (tr > 0.0f) {
                const f32 k = std::sqrt(tr + 1.0f) * 2.0f;
                q = {(r.at(2, 1) - r.at(1, 2)) / k, (r.at(0, 2) - r.at(2, 0)) / k, (r.at(1, 0) - r.at(0, 1)) / k,
                     0.25f * k};
            } else if (r.at(0, 0) > r.at(1, 1) && r.at(0, 0) > r.at(2, 2)) {
                const f32 k = std::sqrt(1.0f + r.at(0, 0) - r.at(1, 1) - r.at(2, 2)) * 2.0f;
                q = {0.25f * k, (r.at(0, 1) + r.at(1, 0)) / k, (r.at(0, 2) + r.at(2, 0)) / k,
                     (r.at(2, 1) - r.at(1, 2)) / k};
            } else if (r.at(1, 1) > r.at(2, 2)) {
                const f32 k = std::sqrt(1.0f + r.at(1, 1) - r.at(0, 0) - r.at(2, 2)) * 2.0f;
                q = {(r.at(0, 1) + r.at(1, 0)) / k, 0.25f * k, (r.at(1, 2) + r.at(2, 1)) / k,
                     (r.at(0, 2) - r.at(2, 0)) / k};
            } else {
                const f32 k = std::sqrt(1.0f + r.at(2, 2) - r.at(0, 0) - r.at(1, 1)) * 2.0f;
                q = {(r.at(0, 2) + r.at(2, 0)) / k, (r.at(1, 2) + r.at(2, 1)) / k, 0.25f * k,
                     (r.at(1, 0) - r.at(0, 1)) / k};
            }
            for (cgltf_size i = 0; i < nd->mesh->primitives_count; ++i) {
                const cgltf_primitive& prim = nd->mesh->primitives[i];
                if (prim.type != cgltf_primitive_type_triangles) continue;
                const u32 m = mesh(prim);
                if (m == ~0u) continue;
                Instance inst;
                inst.skin = nd->skin && scene.meshes()[m].skinned ? skin(nd->skin) : kNoSkin;
                inst.position = {static_cast<f64>(t.x), static_cast<f64>(t.y), static_cast<f64>(t.z)};
                inst.rotation = normalize(q);
                inst.scale = s;
                inst.mesh = m;
                inst.material = material(prim.material);
                scene.add_instance(inst);
                ++result.instances;
            }
        }
        for (cgltf_size i = 0; i < nd->children_count; ++i) node(nd->children[i], world_from_asset * l);
    }
};

} // namespace

std::shared_ptr<GltfPrepared> gltf_prepare(const char* path, const GltfLoadOptions& options) {
    auto prep = std::make_shared<GltfPrepared>(path, options);
    cgltf_options opts{};
    if (cgltf_parse_file(&opts, path, &prep->data) != cgltf_result_success) {
        TEFRA_LOG_ERROR("glTF: cannot parse %s", path);
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, prep->data, path) != cgltf_result_success) {
        TEFRA_LOG_ERROR("glTF: cannot load buffers of %s", path);
        return nullptr;
    }
    for (cgltf_size i = 0; i < prep->data->meshes_count; ++i)
        for (cgltf_size j = 0; j < prep->data->meshes[i].primitives_count; ++j) {
            const cgltf_primitive& prim = prep->data->meshes[i].primitives[j];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            prep->ordinal[&prim] = static_cast<u32>(prep->primitives.size());
            prep->primitives.push_back(&prim);
        }
    prep->meshes.resize(prep->primitives.size());

    const u64 t0 = time_now_ns();
    const std::string key = cache_key(path, options);
    const std::string file = cache_file(path, key);
    read_cache(file, key, prep->meshes);
    u32 cached = 0;
    std::vector<u32> todo;
    for (u32 i = 0; i < prep->meshes.size(); ++i) {
        if (prep->meshes[i].valid)
            ++cached;
        else
            todo.push_back(i);
    }
    // Primitives are independent: decode and simplify them in parallel (jobs; the caller helps while it waits).
    global_jobs().parallel_for(static_cast<u32>(todo.size()), 1, [&](u32 begin, u32 end) {
        for (u32 k = begin; k < end; ++k) {
            const u32 i = todo[k];
            build_primitive(*prep->primitives[i], options, prep->meshes[i]);
        }
    });
    bool fresh_static = false;
    for (u32 i : todo) fresh_static = fresh_static || (prep->meshes[i].valid && !prep->meshes[i].skinned);
    if (fresh_static) write_cache(file, key, prep->meshes);
    if (cached || !todo.empty())
        TEFRA_LOG_INFO("glTF %s: %zu primitives, %u from the mesh cache, %zu built in %.0f ms", path,
                       prep->primitives.size(), cached, todo.size(), ns_to_ms(time_now_ns() - t0));
    return prep;
}

GltfLoadResult gltf_commit(Scene& scene, GltfPrepared& prep) {
    Loader L(scene, prep);
    const GltfLoadOptions& options = prep.options;
    // Asset placement: world position is applied in double precision on the instances afterwards.
    const mat4 root = mat4_from_quat(options.rotation) * scale(vec3{options.scale});
    L.placement = root;
    const usize first_instance = scene.instances().size();
    const cgltf_scene* sc =
        prep.data->scene ? prep.data->scene : (prep.data->scenes_count ? &prep.data->scenes[0] : nullptr);
    if (sc) {
        for (cgltf_size i = 0; i < sc->nodes_count; ++i) L.node(sc->nodes[i], root);
    } else {
        for (cgltf_size i = 0; i < prep.data->nodes_count; ++i)
            if (!prep.data->nodes[i].parent) L.node(&prep.data->nodes[i], root);
    }
    for (usize i = first_instance; i < scene.instances().size(); ++i)
        scene.instances()[i].position = scene.instances()[i].position + options.position;
    L.result.ok = true;
    TEFRA_LOG_INFO("glTF %s: %u meshes, %u materials, %u textures, %u instances, %llu triangles", prep.path.c_str(),
                   L.result.meshes, L.result.materials, L.result.textures, L.result.instances,
                   static_cast<unsigned long long>(L.result.triangles));
    return L.result;
}

GltfLoadResult load_gltf(Scene& scene, const char* path, const GltfLoadOptions& options) {
    const auto prep = gltf_prepare(path, options);
    if (!prep) return {};
    return gltf_commit(scene, *prep);
}

} // namespace tefra::render
