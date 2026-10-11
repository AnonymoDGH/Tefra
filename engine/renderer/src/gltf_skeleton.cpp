#include "tefra/renderer/gltf_skeleton.h"

#include "tefra/core/log.h"

#include <cgltf.h>

#include <algorithm>
#include <cmath>

namespace tefra::render {
namespace {

mat4 local_matrix(const cgltf_node* node) {
    f32 m[16];
    cgltf_node_transform_local(node, m);
    mat4 out;
    for (int c = 0; c < 4; ++c) out.c[c] = {m[c * 4 + 0], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]};
    return out;
}

// World transform of the ancestors of a node (not including the node itself).
mat4 ancestors_world(const cgltf_node* node) {
    if (!node || !node->parent) return mat4{};
    return ancestors_world(node->parent) * local_matrix(node->parent);
}

int joint_index(const cgltf_skin* skin, const cgltf_node* node) {
    for (cgltf_size i = 0; i < skin->joints_count; ++i)
        if (skin->joints[i] == node) return static_cast<int>(i);
    return -1;
}

void read_channels(const cgltf_skin* skin, GltfAnimation& anim, const cgltf_animation& src) {
    for (cgltf_size c = 0; c < src.channels_count; ++c) {
        const cgltf_animation_channel& ch = src.channels[c];
        if (!ch.target_node || !ch.sampler || !ch.sampler->input || !ch.sampler->output) continue;
        const int ji = joint_index(skin, ch.target_node);
        if (ji < 0) continue;
        GltfChannel g;
        g.joint = static_cast<u32>(ji);
        switch (ch.target_path) {
        case cgltf_animation_path_type_translation: g.path = GltfPath::Translation; break;
        case cgltf_animation_path_type_rotation: g.path = GltfPath::Rotation; break;
        case cgltf_animation_path_type_scale: g.path = GltfPath::Scale; break;
        default: continue; // morph weights are not used
        }
        switch (ch.sampler->interpolation) {
        case cgltf_interpolation_type_step: g.interp = GltfInterp::Step; break;
        case cgltf_interpolation_type_cubic_spline: g.interp = GltfInterp::CubicSpline; break;
        default: g.interp = GltfInterp::Linear; break;
        }
        const cgltf_accessor* in = ch.sampler->input;
        const cgltf_accessor* outa = ch.sampler->output;
        g.times.resize(in->count);
        cgltf_accessor_unpack_floats(in, g.times.data(), in->count);
        g.values.resize(outa->count * cgltf_num_components(outa->type));
        cgltf_accessor_unpack_floats(outa, g.values.data(), g.values.size());
        if (!g.times.empty()) anim.duration = std::max(anim.duration, g.times.back());
        anim.channels.push_back(std::move(g));
    }
}

void read_bounds(const cgltf_data* data, const cgltf_skin* skin, GltfSkeleton& out) {
    bool first = true;
    for (cgltf_size m = 0; m < data->nodes_count; ++m) {
        const cgltf_node& nd = data->nodes[m];
        if (nd.skin != skin || !nd.mesh) continue;
        for (cgltf_size p = 0; p < nd.mesh->primitives_count; ++p) {
            const cgltf_primitive& prim = nd.mesh->primitives[p];
            for (cgltf_size k = 0; k < prim.attributes_count; ++k) {
                if (prim.attributes[k].type != cgltf_attribute_type_position || !prim.attributes[k].data) continue;
                const cgltf_accessor* acc = prim.attributes[k].data;
                std::vector<f32> pos(acc->count * 3);
                cgltf_accessor_unpack_floats(acc, pos.data(), pos.size());
                for (usize v = 0; v + 2 < pos.size(); v += 3) {
                    const vec3 q{pos[v], pos[v + 1], pos[v + 2]};
                    if (first) {
                        out.bounds_min = out.bounds_max = q;
                        first = false;
                    }
                    out.bounds_min = {min(out.bounds_min.x, q.x), min(out.bounds_min.y, q.y),
                                      min(out.bounds_min.z, q.z)};
                    out.bounds_max = {max(out.bounds_max.x, q.x), max(out.bounds_max.y, q.y),
                                      max(out.bounds_max.z, q.z)};
                }
            }
        }
    }
}

} // namespace

void decompose_trs(const mat4& m, vec3& t, quat& r, vec3& s) {
    t = m.c[3].xyz();
    const vec3 sx = m.c[0].xyz(), sy = m.c[1].xyz(), sz = m.c[2].xyz();
    s = {length(sx), length(sy), length(sz)};
    s.x = s.x > 1e-12f ? s.x : 1.0f;
    s.y = s.y > 1e-12f ? s.y : 1.0f;
    s.z = s.z > 1e-12f ? s.z : 1.0f;
    mat4 rot;
    rot.c[0] = vec4(sx / s.x, 0.0f);
    rot.c[1] = vec4(sy / s.y, 0.0f);
    rot.c[2] = vec4(sz / s.z, 0.0f);
    // Rotation matrix -> quaternion (Shepperd's method).
    const f32 tr = rot.at(0, 0) + rot.at(1, 1) + rot.at(2, 2);
    if (tr > 0.0f) {
        const f32 k = std::sqrt(tr + 1.0f) * 2.0f;
        r = {(rot.at(2, 1) - rot.at(1, 2)) / k, (rot.at(0, 2) - rot.at(2, 0)) / k, (rot.at(1, 0) - rot.at(0, 1)) / k,
             0.25f * k};
    } else if (rot.at(0, 0) > rot.at(1, 1) && rot.at(0, 0) > rot.at(2, 2)) {
        const f32 k = std::sqrt(1.0f + rot.at(0, 0) - rot.at(1, 1) - rot.at(2, 2)) * 2.0f;
        r = {0.25f * k, (rot.at(0, 1) + rot.at(1, 0)) / k, (rot.at(0, 2) + rot.at(2, 0)) / k,
             (rot.at(2, 1) - rot.at(1, 2)) / k};
    } else if (rot.at(1, 1) > rot.at(2, 2)) {
        const f32 k = std::sqrt(1.0f + rot.at(1, 1) - rot.at(0, 0) - rot.at(2, 2)) * 2.0f;
        r = {(rot.at(0, 1) + rot.at(1, 0)) / k, 0.25f * k, (rot.at(1, 2) + rot.at(2, 1)) / k,
             (rot.at(0, 2) - rot.at(2, 0)) / k};
    } else {
        const f32 k = std::sqrt(1.0f + rot.at(2, 2) - rot.at(0, 0) - rot.at(1, 1)) * 2.0f;
        r = {(rot.at(0, 2) + rot.at(2, 0)) / k, (rot.at(1, 2) + rot.at(2, 1)) / k, 0.25f * k,
             (rot.at(1, 0) - rot.at(0, 1)) / k};
    }
    r = normalize(r);
}

bool read_gltf_skin(const cgltf_data* data, const cgltf_skin* skin, GltfSkeleton& out) {
    out = GltfSkeleton{};
    if (!data || !skin || skin->joints_count == 0) return false;
    out.skin_name = skin->name ? skin->name : "";
    const cgltf_size n = skin->joints_count;

    std::vector<f32> ibm(n * 16, 0.0f);
    if (skin->inverse_bind_matrices) cgltf_accessor_unpack_floats(skin->inverse_bind_matrices, ibm.data(), n * 16);

    out.joints.resize(n);
    for (cgltf_size i = 0; i < n; ++i) {
        const cgltf_node* node = skin->joints[i];
        GltfJoint& j = out.joints[i];
        j.name = node->name ? node->name : "joint" + std::to_string(i);
        // Nearest joint ancestor is the parent; the non-joint nodes above a root become its base transform.
        j.parent = -1;
        for (const cgltf_node* p = node->parent; p; p = p->parent) {
            const int pi = joint_index(skin, p);
            if (pi >= 0) {
                j.parent = pi;
                break;
            }
        }
        j.base = j.parent < 0 ? ancestors_world(node) : mat4{};
        decompose_trs(local_matrix(node), j.t, j.r, j.s);
        if (skin->inverse_bind_matrices)
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row) j.inverse_bind.c[c][row] = ibm[i * 16 + usize(c * 4 + row)];
    }

    for (cgltf_size a = 0; a < data->animations_count; ++a) {
        GltfAnimation anim;
        anim.name = data->animations[a].name ? data->animations[a].name : "animation" + std::to_string(a);
        read_channels(skin, anim, data->animations[a]);
        out.animations.push_back(std::move(anim));
    }
    read_bounds(data, skin, out);
    out.ok = true;
    return true;
}

bool load_gltf_skeleton(const char* path, GltfSkeleton& out) {
    out = GltfSkeleton{};
    cgltf_options opts{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        TEFRA_LOG_ERROR("glTF: cannot parse %s", path);
        return false;
    }
    bool ok = false;
    if (cgltf_load_buffers(&opts, data, path) == cgltf_result_success) {
        const cgltf_skin* skin = nullptr;
        for (cgltf_size i = 0; i < data->nodes_count && !skin; ++i) skin = data->nodes[i].skin;
        if (skin)
            ok = read_gltf_skin(data, skin, out);
        else
            TEFRA_LOG_WARN("glTF %s: no skin", path);
    } else {
        TEFRA_LOG_ERROR("glTF: cannot load buffers of %s", path);
    }
    cgltf_free(data);
    return ok;
}

namespace {

// Global joint matrix, parents first (the joint list of a glTF skin is not necessarily sorted).
mat4 global_of(const GltfSkeleton& skel, std::vector<mat4>& global, std::vector<u8>& done, usize j) {
    if (done[j]) return global[j];
    const GltfJoint& jt = skel.joints[j];
    const mat4 parent = jt.parent >= 0 ? global_of(skel, global, done, usize(jt.parent)) : jt.base;
    global[j] = parent * compose_trs(jt.t, jt.r, jt.s);
    done[j] = 1;
    return global[j];
}

} // namespace

std::vector<mat4> rest_palette(const GltfSkeleton& skel) {
    std::vector<mat4> global(skel.joints.size());
    std::vector<u8> done(skel.joints.size(), 0);
    std::vector<mat4> palette(skel.joints.size());
    for (usize j = 0; j < skel.joints.size(); ++j)
        palette[j] = global_of(skel, global, done, j) * skel.joints[j].inverse_bind;
    return palette;
}

} // namespace tefra::render
