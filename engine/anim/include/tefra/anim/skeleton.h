#pragma once

// Skeletal animation for imported characters (spec M3b, ADR-016): joints from a glTF skin, keyframe clips with
// translation, rotation and scale channels (LINEAR, STEP and CUBICSPLINE), and the palette that the GPU skinning pass
// consumes. The R15 avatar keeps its Motor6D path (clip.h, animator.h); this file is the other path.

#include "tefra/anim/transform.h"
#include "tefra/core/math.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::anim {

// Translation, rotation and scale of one joint in its parent's space.
struct Trs {
    vec3 t{};
    quat r{0.0f, 0.0f, 0.0f, 1.0f};
    vec3 s{1.0f, 1.0f, 1.0f};
};

Trs blend(const Trs& a, const Trs& b, f32 k); // k = weight of b (0 keeps a)
mat4 to_mat4(const Trs& x);

struct Joint {
    std::string name;
    i32 parent = -1; // index into Skeleton::joints, -1 for a root
    mat4 base{};     // roots: world transform of the non-joint ancestors (identity otherwise)
    Trs rest;
    mat4 inverse_bind{}; // from the skin; identity when the file has none
};

struct Skeleton {
    std::vector<Joint> joints;
    i32 find(std::string_view name) const; // -1 when absent
    std::vector<Trs> rest_pose() const;
    // out[j] = global_j * inverse_bind_j, with global_j = global_parent * to_mat4(pose[j]) (roots: base * local).
    // These are the joint matrices of the skinning pass, in the mesh's own space.
    void palette(const std::vector<Trs>& pose, std::vector<mat4>& out) const;
};

enum class Target : u8 { Translation, Rotation, Scale };
enum class Interp : u8 { Linear, Step, Cubic };

struct Channel {
    u32 joint = 0;
    Target target = Target::Translation;
    Interp interp = Interp::Linear;
    std::vector<f32> times;  // key times (s)
    std::vector<f32> values; // 3 floats per value (4 for rotation); cubic: in-tangent, value, out-tangent per key
};

struct SkeletalClip {
    std::string name;
    f32 duration = 0.0f;
    std::vector<Channel> channels;

    // Writes into `out` the components this clip animates for `joint` at `time` (other components are kept).
    // Looped clips wrap time, other clips hold their last key. False when no channel targets the joint.
    bool sample_joint(u32 joint, f32 time, bool looped, Trs& out) const;
    bool animates(u32 joint) const;
};
using SkeletalClipPtr = std::shared_ptr<const SkeletalClip>;

struct SkeletonAsset {
    bool ok = false;
    std::string path;
    Skeleton skeleton;
    std::vector<SkeletalClipPtr> clips;
    vec3 bounds_min{};
    vec3 bounds_max{};
};

// Reads the first skin and its animations of a .gltf/.glb file.
bool load_skeleton_asset(const char* path, SkeletonAsset& out);
// Clip index by name (case-insensitive exact match, then substring) or "#<index>". -1 when absent.
i32 find_clip(const SkeletonAsset& asset, std::string_view name);

} // namespace tefra::anim
