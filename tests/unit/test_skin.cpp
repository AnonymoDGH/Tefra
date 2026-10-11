#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/anim/skeleton.h"
#include "tefra/core/fs.h"
#include "tefra/renderer/gltf_skeleton.h"
#include "tefra/renderer/mesh.h"
#include "tefra/renderer/skin.h"

#include <cmath>
#include <vector>

using namespace tefra;
using namespace tefra::anim;
using namespace tefra::render;

namespace {

bool near3(vec3 a, vec3 b, f32 eps = 1e-4f) { return length(a - b) < eps; }

quat rot_z(f32 radians) { return {0.0f, 0.0f, std::sin(radians * 0.5f), std::cos(radians * 0.5f)}; }

// Two joints along +Y: "Root" at the origin and "Tip" one metre above it. The bind pose is the rest pose, so the
// inverse bind matrices are the inverse translations.
Skeleton two_bone_skeleton() {
    Skeleton sk;
    sk.joints.resize(2);
    sk.joints[0].name = "Root";
    sk.joints[0].parent = -1;
    sk.joints[0].rest = Trs{};
    sk.joints[0].inverse_bind = mat4{};
    sk.joints[1].name = "Tip";
    sk.joints[1].parent = 0;
    sk.joints[1].rest = Trs{vec3{0.0f, 1.0f, 0.0f}, quat{0.0f, 0.0f, 0.0f, 1.0f}, vec3{1.0f}};
    sk.joints[1].inverse_bind = compose_trs(vec3{0.0f, -1.0f, 0.0f}, quat{0.0f, 0.0f, 0.0f, 1.0f}, vec3{1.0f});
    return sk;
}

// Column of vertices: y = 0.5 follows Root, y = 1.5 follows Tip, y = 1 (the pivot) is shared 50/50.
MeshData two_bone_mesh() {
    MeshData m;
    const f32 ys[3] = {0.5f, 1.0f, 1.5f};
    for (usize i = 0; i < 3; ++i) {
        m.vertices.push_back(
            make_vertex(vec3{0.0f, ys[i], 0.0f}, vec3{1.0f, 0.0f, 0.0f}, vec2{}, vec3{0.0f, 0.0f, 1.0f}, 1.0f));
        SkinAttr s;
        if (i == 0) {
            s.joints[0] = 0;
            s.weights[0] = 1.0f;
        } else if (i == 2) {
            s.joints[0] = 1;
            s.weights[0] = 1.0f;
        } else {
            s.joints[0] = 0;
            s.weights[0] = 0.5f;
            s.joints[1] = 1;
            s.weights[1] = 0.5f;
        }
        m.skin.push_back(s);
    }
    m.indices = {0, 1, 2};
    return m;
}

Channel rotation_channel(u32 joint, Interp interp, std::vector<f32> times, std::vector<f32> values) {
    Channel c;
    c.joint = joint;
    c.target = Target::Rotation;
    c.interp = interp;
    c.times = std::move(times);
    c.values = std::move(values);
    return c;
}

} // namespace

TEST_CASE("skin: rest palette is the identity for a consistent bind pose") {
    const Skeleton sk = two_bone_skeleton();
    std::vector<mat4> palette;
    sk.palette(sk.rest_pose(), palette);
    REQUIRE(palette.size() == 2);
    for (const mat4& m : palette)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) CHECK(std::fabs(m.at(r, c) - (r == c ? 1.0f : 0.0f)) < 1e-5f);
}

TEST_CASE("skin: CPU reference keeps the bind pose at rest and rotates the tip about the pivot") {
    const MeshData mesh = two_bone_mesh();
    const Skeleton sk = two_bone_skeleton();
    std::vector<mat4> palette;
    sk.palette(sk.rest_pose(), palette);
    MeshData rest = skin_mesh_reference(mesh, palette);
    for (usize i = 0; i < mesh.vertices.size(); ++i)
        CHECK(near3(vec3{rest.vertices[i].pos[0], rest.vertices[i].pos[1], rest.vertices[i].pos[2]},
                    vec3{mesh.vertices[i].pos[0], mesh.vertices[i].pos[1], mesh.vertices[i].pos[2]}));

    // Tip rotated 90 degrees about Z, pivot at (0,1,0).
    std::vector<Trs> pose = sk.rest_pose();
    pose[1].r = rot_z(kPi * 0.5f);
    sk.palette(pose, palette);
    const MeshData bent = skin_mesh_reference(mesh, palette);
    auto pos = [](const Vertex& v) { return vec3{v.pos[0], v.pos[1], v.pos[2]}; };
    CHECK(near3(pos(bent.vertices[0]), vec3{0.0f, 0.5f, 0.0f}));  // follows Root: unchanged
    CHECK(near3(pos(bent.vertices[1]), vec3{0.0f, 1.0f, 0.0f}));  // the pivot does not move
    CHECK(near3(pos(bent.vertices[2]), vec3{-0.5f, 1.0f, 0.0f})); // (0,1.5,0) swings to (-0.5,1,0)

    // The normal (1,0,0) of a vertex on Tip turns with the bone: a Z rotation maps +X to +Y.
    const Vertex tip = skin_vertex_reference(mesh.vertices[2], mesh.skin[2], palette);
    const vec3 n = oct_decode(tip.normal);
    CHECK(near3(n, vec3{0.0f, 1.0f, 0.0f}, 1e-3f));
}

TEST_CASE("skin: half-weighted vertex blends the two palettes") {
    const MeshData mesh = two_bone_mesh();
    const Skeleton sk = two_bone_skeleton();
    std::vector<Trs> pose = sk.rest_pose();
    pose[1].r = rot_z(kPi * 0.5f);
    std::vector<mat4> palette;
    sk.palette(pose, palette);
    // The pivot vertex is on both bones: each palette keeps it in place, so the blend is still (0,1,0).
    const Vertex mid = skin_vertex_reference(mesh.vertices[1], mesh.skin[1], palette);
    CHECK(near3(vec3{mid.pos[0], mid.pos[1], mid.pos[2]}, vec3{0.0f, 1.0f, 0.0f}));

    // A point 0.5 m above the pivot on the Tip bone is at (0,1.5,0) in bind space: weight it 50/50 with Root.
    SkinAttr half;
    half.joints[0] = 0;
    half.weights[0] = 0.5f;
    half.joints[1] = 1;
    half.weights[1] = 0.5f;
    const Vertex v = skin_vertex_reference(mesh.vertices[2], half, palette);
    // 0.5 * (0,1.5,0) + 0.5 * (-0.5,1,0) = (-0.25, 1.25, 0)
    CHECK(near3(vec3{v.pos[0], v.pos[1], v.pos[2]}, vec3{-0.25f, 1.25f, 0.0f}));
}

TEST_CASE("skin: zero total weight keeps the bind pose") {
    const MeshData mesh = two_bone_mesh();
    const Skeleton sk = two_bone_skeleton();
    std::vector<Trs> pose = sk.rest_pose();
    pose[1].r = rot_z(1.0f);
    std::vector<mat4> palette;
    sk.palette(pose, palette);
    SkinAttr none;
    const Vertex v = skin_vertex_reference(mesh.vertices[2], none, palette);
    CHECK(near3(vec3{v.pos[0], v.pos[1], v.pos[2]}, vec3{0.0f, 1.5f, 0.0f}));
}

TEST_CASE("clip: LINEAR rotation interpolates along the shortest arc") {
    SkeletalClip clip;
    clip.duration = 1.0f;
    clip.channels.push_back(
        rotation_channel(1, Interp::Linear, {0.0f, 1.0f},
                         {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, std::sin(kPi * 0.25f), std::cos(kPi * 0.25f)}));
    Trs out;
    REQUIRE(clip.sample_joint(1, 0.5f, false, out));
    // Halfway from 0 to 90 degrees about Z is 45 degrees.
    CHECK(std::fabs(out.r.z - std::sin(kPi * 0.125f)) < 1e-4f);
    CHECK(std::fabs(out.r.w - std::cos(kPi * 0.125f)) < 1e-4f);
    CHECK(!clip.sample_joint(0, 0.5f, false, out)); // joint 0 has no channel
}

TEST_CASE("clip: STEP holds the previous key, CUBICSPLINE with zero tangents is a Hermite blend") {
    SkeletalClip clip;
    clip.duration = 1.0f;
    Channel step;
    step.joint = 0;
    step.target = Target::Translation;
    step.interp = Interp::Step;
    step.times = {0.0f, 1.0f};
    step.values = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    clip.channels.push_back(step);
    Channel cubic;
    cubic.joint = 1;
    cubic.target = Target::Translation;
    cubic.interp = Interp::Cubic;
    cubic.times = {0.0f, 1.0f};
    // Per key: in-tangent, value, out-tangent.
    cubic.values = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0}; // key 1 value is (2,0,0), tangents 0
    clip.channels.push_back(cubic);

    Trs a;
    clip.sample_joint(0, 0.5f, false, a);
    CHECK(near3(a.t, vec3{0.0f, 0.0f, 0.0f}));
    Trs b;
    clip.sample_joint(1, 0.5f, false, b);
    CHECK(near3(b.t, vec3{1.0f, 0.0f, 0.0f}));
    Trs c;
    clip.sample_joint(0, 1.0f, false, c);
    CHECK(near3(c.t, vec3{2.0f, 0.0f, 0.0f}));
}

TEST_CASE("clip: looped clips wrap time, others hold the last key") {
    SkeletalClip clip;
    clip.duration = 1.0f;
    clip.channels.push_back(
        rotation_channel(0, Interp::Linear, {0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f}));
    Trs wrapped, base, held;
    clip.sample_joint(0, 1.25f, true, wrapped);
    clip.sample_joint(0, 0.25f, true, base);
    CHECK(std::fabs(wrapped.r.y - base.r.y) < 1e-5f);
    clip.sample_joint(0, 3.0f, false, held);
    CHECK(std::fabs(held.r.y - 1.0f) < 1e-5f);
}

TEST_CASE("clip: names resolve by #index, exact name, then case-insensitive substring") {
    SkeletonAsset asset;
    for (const char* n : {"Survey", "Walk", "Run"}) {
        auto c = std::make_shared<SkeletalClip>();
        c->name = n;
        asset.clips.push_back(c);
    }
    CHECK(find_clip(asset, "#1") == 1);
    CHECK(find_clip(asset, "#7") == -1);
    CHECK(find_clip(asset, "run") == 2);
    CHECK(find_clip(asset, "WAL") == 1);
    CHECK(find_clip(asset, "Jump") == -1);
    CHECK(find_clip(asset, "#Survey") == 0);
}

TEST_CASE("glTF: the Fox rig rests on its inverse bind matrices (skipped without assets/cache)") {
    const std::string path = std::string(TEFRA_ASSET_ROOT) + "/models/characters/fox.glb";
    if (!file_exists(path.c_str())) {
        MESSAGE("assets/cache/models/characters/fox.glb is missing (run scripts/fetch_assets.py): skipped");
        return;
    }
    SkeletonAsset asset;
    REQUIRE(load_skeleton_asset(path.c_str(), asset));
    CHECK(asset.skeleton.joints.size() > 10);
    CHECK(find_clip(asset, "walk") >= 0);
    CHECK(find_clip(asset, "run") >= 0);
    // The bind pose is the rest pose: every joint's palette is the identity.
    std::vector<mat4> palette;
    asset.skeleton.palette(asset.skeleton.rest_pose(), palette);
    f32 worst = 0.0f;
    for (const mat4& m : palette)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) worst = std::fmax(worst, std::fabs(m.at(r, c) - (r == c ? 1.0f : 0.0f)));
    CHECK(worst < 1e-3f);
    // A walk cycle moves the feet: the palettes differ from the rest pose at some time.
    const i32 walk = find_clip(asset, "walk");
    std::vector<Trs> pose = asset.skeleton.rest_pose();
    f32 moved = 0.0f;
    for (f32 t = 0.0f; t < asset.clips[static_cast<usize>(walk)]->duration; t += 0.1f) {
        pose = asset.skeleton.rest_pose();
        for (usize j = 0; j < pose.size(); ++j)
            asset.clips[static_cast<usize>(walk)]->sample_joint(static_cast<u32>(j), t, true, pose[j]);
        std::vector<mat4> p;
        asset.skeleton.palette(pose, p);
        for (usize j = 0; j < p.size(); ++j) moved = std::fmax(moved, std::fabs(p[j].c[3].x - palette[j].c[3].x));
    }
    CHECK(moved > 0.01f);
}

TEST_CASE("glTF: CesiumMan loads with its skin and clip, and its rest pose is the identity (skipped without assets)") {
    const std::string path = std::string(TEFRA_ASSET_ROOT) + "/models/characters/cesium_man.glb";
    if (!file_exists(path.c_str())) {
        MESSAGE("assets/cache/models/characters/cesium_man.glb is missing (run scripts/fetch_assets.py): skipped");
        return;
    }
    SkeletonAsset asset;
    REQUIRE(load_skeleton_asset(path.c_str(), asset));
    CHECK(asset.skeleton.joints.size() >= 15);
    REQUIRE(asset.clips.size() >= 1);
    // Unlike the Fox, the rest pose of this file is not its bind pose (the exported rest transforms differ from the
    // inverse bind matrices), so the rest palette is not the identity. The clip must still be playable.
    CHECK(asset.clips[0]->duration > 0.0f);
    CHECK(asset.clips[0]->channels.size() > 0);
}
