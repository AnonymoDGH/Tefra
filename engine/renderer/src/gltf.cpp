#include "tefra/renderer/gltf.h"
#include "tefra/renderer/scene.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/rhi/upload.h"

#include <cgltf.h>

#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tefra::render {
namespace {

struct Loader {
    Scene& scene;
    const rhi::Device& dev;
    std::string base_dir;
    cgltf_data* data = nullptr;
    std::map<std::pair<const cgltf_image*, bool>, u32> texture_cache; // (image, srgb) -> bindless
    std::map<const cgltf_material*, u32> material_cache;
    std::map<const cgltf_primitive*, u32> mesh_cache;
    GltfLoadResult result;

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
        if (has_mr && decode_image(mr.texture->image, orm) && orm.width == occ_px.width && orm.height == occ_px.height) {
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

    template <typename T, usize N> bool read_attr(const cgltf_accessor* acc, std::vector<T>& out) {
        out.resize(acc->count);
        for (cgltf_size i = 0; i < acc->count; ++i)
            if (!cgltf_accessor_read_float(acc, i, &out[i][0], N)) return false;
        return true;
    }

    u32 mesh(const cgltf_primitive& prim) {
        if (auto it = mesh_cache.find(&prim); it != mesh_cache.end()) return it->second;
        const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tan = nullptr, *uv = nullptr;
        for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
            const cgltf_attribute& a = prim.attributes[i];
            if (a.type == cgltf_attribute_type_position) pos = a.data;
            else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
            else if (a.type == cgltf_attribute_type_tangent) tan = a.data;
            else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
        }
        if (!pos) return ~0u;
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

        MeshData md;
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
        result.triangles += md.indices.size() / 3;
        ++result.meshes;
        return mesh_cache[&prim] = scene.add_mesh(md);
    }

    void node(const cgltf_node* nd, const mat4& world_from_asset) {
        f32 local[16];
        cgltf_node_transform_local(nd, local);
        mat4 l;
        for (int c = 0; c < 4; ++c) l.c[c] = {local[c * 4 + 0], local[c * 4 + 1], local[c * 4 + 2], local[c * 4 + 3]};
        const mat4 world = world_from_asset * l;
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
                q = {(r.at(2, 1) - r.at(1, 2)) / k, (r.at(0, 2) - r.at(2, 0)) / k, (r.at(1, 0) - r.at(0, 1)) / k, 0.25f * k};
            } else if (r.at(0, 0) > r.at(1, 1) && r.at(0, 0) > r.at(2, 2)) {
                const f32 k = std::sqrt(1.0f + r.at(0, 0) - r.at(1, 1) - r.at(2, 2)) * 2.0f;
                q = {0.25f * k, (r.at(0, 1) + r.at(1, 0)) / k, (r.at(0, 2) + r.at(2, 0)) / k, (r.at(2, 1) - r.at(1, 2)) / k};
            } else if (r.at(1, 1) > r.at(2, 2)) {
                const f32 k = std::sqrt(1.0f + r.at(1, 1) - r.at(0, 0) - r.at(2, 2)) * 2.0f;
                q = {(r.at(0, 1) + r.at(1, 0)) / k, 0.25f * k, (r.at(1, 2) + r.at(2, 1)) / k, (r.at(0, 2) - r.at(2, 0)) / k};
            } else {
                const f32 k = std::sqrt(1.0f + r.at(2, 2) - r.at(0, 0) - r.at(1, 1)) * 2.0f;
                q = {(r.at(0, 2) + r.at(2, 0)) / k, (r.at(1, 2) + r.at(2, 1)) / k, 0.25f * k, (r.at(1, 0) - r.at(0, 1)) / k};
            }
            for (cgltf_size i = 0; i < nd->mesh->primitives_count; ++i) {
                const cgltf_primitive& prim = nd->mesh->primitives[i];
                if (prim.type != cgltf_primitive_type_triangles) continue;
                const u32 m = mesh(prim);
                if (m == ~0u) continue;
                Instance inst;
                inst.position = {static_cast<f64>(t.x), static_cast<f64>(t.y), static_cast<f64>(t.z)};
                inst.rotation = normalize(q);
                inst.scale = s;
                inst.mesh = m;
                inst.material = material(prim.material);
                scene.add_instance(inst);
                ++result.instances;
            }
        }
        for (cgltf_size i = 0; i < nd->children_count; ++i) node(nd->children[i], world);
    }
};

} // namespace

GltfLoadResult load_gltf(Scene& scene, const char* path, const GltfLoadOptions& options) {
    Loader L{scene, scene.device(), path_parent(path)};
    cgltf_options opts{};
    if (cgltf_parse_file(&opts, path, &L.data) != cgltf_result_success) {
        TEFRA_LOG_ERROR("glTF: cannot parse %s", path);
        return {};
    }
    if (cgltf_load_buffers(&opts, L.data, path) != cgltf_result_success) {
        TEFRA_LOG_ERROR("glTF: cannot load buffers of %s", path);
        cgltf_free(L.data);
        return {};
    }
    // Asset placement: world position is applied in double precision on the instances afterwards.
    const mat4 root = mat4_from_quat(options.rotation) * scale(vec3{options.scale});
    const usize first_instance = scene.instances().size();
    const cgltf_scene* sc = L.data->scene ? L.data->scene : (L.data->scenes_count ? &L.data->scenes[0] : nullptr);
    if (sc) {
        for (cgltf_size i = 0; i < sc->nodes_count; ++i) L.node(sc->nodes[i], root);
    } else {
        for (cgltf_size i = 0; i < L.data->nodes_count; ++i)
            if (!L.data->nodes[i].parent) L.node(&L.data->nodes[i], root);
    }
    for (usize i = first_instance; i < scene.instances().size(); ++i)
        scene.instances()[i].position = scene.instances()[i].position + options.position;
    cgltf_free(L.data);
    L.result.ok = true;
    TEFRA_LOG_INFO("glTF %s: %u meshes, %u materials, %u textures, %u instances, %llu triangles", path, L.result.meshes,
                   L.result.materials, L.result.textures, L.result.instances,
                   static_cast<unsigned long long>(L.result.triangles));
    return L.result;
}

} // namespace tefra::render
