#include "tefra/anim/skeleton.h"

#include "tefra/renderer/gltf_skeleton.h"

#include <algorithm>
#include <cmath>

namespace tefra::anim {
namespace {

u32 components(Target t) { return t == Target::Rotation ? 4u : 3u; }

// Index of the key interval [times[i], times[i+1]) that contains t, clamped to the keys.
usize key_index(const std::vector<f32>& times, f32 t) {
    const auto it = std::upper_bound(times.begin(), times.end(), t);
    if (it == times.begin()) return 0;
    return std::min(static_cast<usize>(it - times.begin()) - 1, times.size() - 1);
}

// Value number `index` of the channel (vectors are stored as consecutive floats).
vec4 value_at(const Channel& c, usize index, u32 n) {
    vec4 v{0.0f, 0.0f, 0.0f, 0.0f};
    f32* out = &v.x;
    for (u32 k = 0; k < n; ++k) out[k] = c.values[index * n + k];
    return v;
}

// Evaluates a channel at time t (already wrapped or clamped). Returns the components in xyzw order.
vec4 eval_channel(const Channel& c, f32 t) {
    const u32 n = components(c.target);
    const usize keys = c.times.size();
    if (c.interp == Interp::Cubic) {
        // Three values per key: in-tangent, value, out-tangent (glTF CUBICSPLINE).
        const usize k = key_index(c.times, t);
        if (k + 1 >= keys || t <= c.times[k]) return value_at(c, k * 3 + 1, n);
        const f32 t0 = c.times[k], t1 = c.times[k + 1], dt = t1 - t0;
        const f32 s = (t - t0) / dt;
        const f32 s2 = s * s, s3 = s2 * s;
        const vec4 p0 = value_at(c, k * 3 + 1, n), m0 = value_at(c, k * 3 + 2, n);
        const vec4 p1 = value_at(c, (k + 1) * 3 + 1, n), m1 = value_at(c, (k + 1) * 3, n);
        const f32 h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
        return p0 * h00 + m0 * (h10 * dt) + p1 * h01 + m1 * (h11 * dt);
    }
    if (keys == 1 || t <= c.times.front()) return value_at(c, 0, n);
    if (t >= c.times.back()) return value_at(c, keys - 1, n);
    const usize k = key_index(c.times, t);
    if (c.interp == Interp::Step) return value_at(c, k, n);
    const f32 t0 = c.times[k], t1 = c.times[k + 1];
    const f32 s = t1 > t0 ? (t - t0) / (t1 - t0) : 0.0f;
    const vec4 a = value_at(c, k, n), b = value_at(c, k + 1, n);
    if (c.target == Target::Rotation) {
        const quat q = slerp(quat{a.x, a.y, a.z, a.w}, quat{b.x, b.y, b.z, b.w}, s);
        return {q.x, q.y, q.z, q.w};
    }
    return a * (1.0f - s) + b * s;
}

// Joint matrices in parent-first order without assuming the joint list is sorted.
void global_of(const Skeleton& sk, const std::vector<Trs>& pose, usize j, std::vector<mat4>& global,
               std::vector<u8>& done) {
    if (done[j]) return;
    const Joint& jt = sk.joints[j];
    mat4 parent_global = jt.base;
    if (jt.parent >= 0) {
        global_of(sk, pose, static_cast<usize>(jt.parent), global, done);
        parent_global = global[static_cast<usize>(jt.parent)];
    }
    global[j] = parent_global * to_mat4(pose[j]);
    done[j] = 1;
}

} // namespace

Trs blend(const Trs& a, const Trs& b, f32 k) {
    Trs out;
    out.t = a.t + (b.t - a.t) * k;
    out.r = slerp(a.r, b.r, k);
    out.s = a.s + (b.s - a.s) * k;
    return out;
}

mat4 to_mat4(const Trs& x) { return compose_trs(x.t, x.r, x.s); }

i32 Skeleton::find(std::string_view name) const {
    for (usize i = 0; i < joints.size(); ++i)
        if (joints[i].name == name) return static_cast<i32>(i);
    return -1;
}

std::vector<Trs> Skeleton::rest_pose() const {
    std::vector<Trs> out(joints.size());
    for (usize i = 0; i < joints.size(); ++i) out[i] = joints[i].rest;
    return out;
}

void Skeleton::palette(const std::vector<Trs>& pose, std::vector<mat4>& out) const {
    std::vector<mat4> global(joints.size());
    std::vector<u8> done(joints.size(), 0);
    for (usize j = 0; j < joints.size(); ++j) global_of(*this, pose, j, global, done);
    out.resize(joints.size());
    for (usize j = 0; j < joints.size(); ++j) out[j] = global[j] * joints[j].inverse_bind;
}

bool SkeletalClip::sample_joint(u32 joint, f32 time, bool looped, Trs& out) const {
    f32 t = time;
    if (duration > 0.0f) {
        if (looped) {
            t = std::fmod(time, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = clamp(time, 0.0f, duration);
        }
    } else {
        t = 0.0f;
    }
    bool any = false;
    for (const Channel& c : channels) {
        if (c.joint != joint || c.times.empty()) continue;
        any = true;
        const vec4 v = eval_channel(c, t);
        switch (c.target) {
        case Target::Translation: out.t = {v.x, v.y, v.z}; break;
        case Target::Rotation: out.r = normalize(quat{v.x, v.y, v.z, v.w}); break;
        case Target::Scale: out.s = {v.x, v.y, v.z}; break;
        }
    }
    return any;
}

bool SkeletalClip::animates(u32 joint) const {
    for (const Channel& c : channels)
        if (c.joint == joint) return true;
    return false;
}

bool load_skeleton_asset(const char* path, SkeletonAsset& out) {
    out = SkeletonAsset{};
    render::GltfSkeleton g;
    if (!render::load_gltf_skeleton(path, g) || g.joints.empty()) return false;
    out.path = path;
    out.bounds_min = g.bounds_min;
    out.bounds_max = g.bounds_max;
    out.skeleton.joints.resize(g.joints.size());
    for (usize i = 0; i < g.joints.size(); ++i) {
        const render::GltfJoint& src = g.joints[i];
        Joint& j = out.skeleton.joints[i];
        j.name = src.name;
        j.parent = src.parent;
        j.base = src.base;
        j.rest = Trs{src.t, src.r, src.s};
        j.inverse_bind = src.inverse_bind;
    }
    for (const render::GltfAnimation& a : g.animations) {
        auto clip = std::make_shared<SkeletalClip>();
        clip->name = a.name;
        clip->duration = a.duration;
        for (const render::GltfChannel& c : a.channels) {
            Channel ch;
            ch.joint = c.joint;
            ch.target = c.path == render::GltfPath::Translation ? Target::Translation
                        : c.path == render::GltfPath::Rotation  ? Target::Rotation
                                                                : Target::Scale;
            ch.interp = c.interp == render::GltfInterp::Step          ? Interp::Step
                        : c.interp == render::GltfInterp::CubicSpline ? Interp::Cubic
                                                                      : Interp::Linear;
            ch.times = c.times;
            ch.values = c.values;
            clip->channels.push_back(std::move(ch));
        }
        out.clips.push_back(std::move(clip));
    }
    out.ok = true;
    return true;
}

namespace {

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return r;
}

} // namespace

i32 find_clip(const SkeletonAsset& asset, std::string_view name) {
    if (name.empty()) return -1;
    if (name[0] == '#') {
        name.remove_prefix(1);
        bool digits = !name.empty();
        u32 index = 0;
        for (char c : name) {
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
            index = index * 10 + static_cast<u32>(c - '0');
        }
        if (digits) return index < asset.clips.size() ? static_cast<i32>(index) : -1;
        if (name.empty()) return -1;
    }
    const std::string want = lower(name);
    for (usize i = 0; i < asset.clips.size(); ++i)
        if (lower(asset.clips[i]->name) == want) return static_cast<i32>(i);
    for (usize i = 0; i < asset.clips.size(); ++i)
        if (lower(asset.clips[i]->name).find(want) != std::string::npos) return static_cast<i32>(i);
    return -1;
}

} // namespace tefra::anim
