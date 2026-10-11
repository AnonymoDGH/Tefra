#pragma once

// Skeleton and keyframe data of a glTF skin (spec M3b, ADR-016), in plain renderer types so that engine/anim can
// convert them into its own Skeleton and clips. The joint order is the skin's joint order, which is also the order
// of JOINTS_0 indices and of the palette.

#include "tefra/core/math.h"

#include <string>
#include <vector>

struct cgltf_data;
struct cgltf_skin;

namespace tefra::render {

enum class GltfPath : u8 { Translation, Rotation, Scale };
enum class GltfInterp : u8 { Linear, Step, CubicSpline };

struct GltfJoint {
    std::string name;
    i32 parent = -1; // nearest joint ancestor (index into joints), -1 for a root
    mat4 base{};     // root joints: world transform of the non-joint ancestors (identity otherwise)
    vec3 t{};        // rest local translation (m)
    quat r{0.0f, 0.0f, 0.0f, 1.0f};
    vec3 s{1.0f, 1.0f, 1.0f};
    mat4 inverse_bind{}; // from the skin's inverseBindMatrices (identity when absent)
};

struct GltfChannel {
    u32 joint = 0;
    GltfPath path = GltfPath::Translation;
    GltfInterp interp = GltfInterp::Linear;
    std::vector<f32> times; // key times (s)
    // Translation and Scale: 3 floats per value. Rotation: 4 floats (xyzw). CubicSpline stores three values per key,
    // in-tangent, value, out-tangent, in that order.
    std::vector<f32> values;
};

struct GltfAnimation {
    std::string name;
    f32 duration = 0.0f; // time of the last key of any channel (s)
    std::vector<GltfChannel> channels;
};

struct GltfSkeleton {
    bool ok = false;
    std::string skin_name;
    std::vector<GltfJoint> joints;
    std::vector<GltfAnimation> animations;
    vec3 bounds_min{}; // bind-pose extent of the skinned primitives (asset units, usually metres)
    vec3 bounds_max{};
};

// Reads the first skin of a .gltf/.glb file. False when the file has no skin or cannot be parsed.
bool load_gltf_skeleton(const char* path, GltfSkeleton& out);
// Reads one skin of a file that cgltf has already parsed (node transforms and buffers loaded).
bool read_gltf_skin(const cgltf_data* data, const cgltf_skin* skin, GltfSkeleton& out);
// Rest palette: joint_global * inverse_bind for the rest local transforms.
std::vector<mat4> rest_palette(const GltfSkeleton& skel);
// Decomposes an affine matrix into translation, rotation and scale (no shear).
void decompose_trs(const mat4& m, vec3& t, quat& r, vec3& s);

} // namespace tefra::render
