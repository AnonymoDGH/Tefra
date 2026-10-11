#pragma once

// Procedural R15-style avatar (spec M3 ADR-016): a Model "Character" with 16 Parts (HumanoidRootPart plus 15 body
// parts), 15 Motor6D joints, a Humanoid and an Animator. The geometry is our own; no third-party rig is used.
//
// Rest pose: every Part is placed in its rest transform and every Motor6D satisfies
//   Part1 = Part0 * C0 * inverse(C1)
// so the rig is closed at rest. With the root at Humanoid.HipHeight above the ground, the soles are on y = origin.y.
// Units: metres. The character faces -Z; its right side is +X.

#include "tefra/core/math.h"
#include "tefra/world/datamodel.h"

#include <array>
#include <string>

namespace tefra::anim {

enum class AvatarPreset : u8 { Explorer, Stocky, Lanky };

struct AvatarDesc {
    std::string name = "Character";
    vec3 origin{0.0f, 0.0f, 0.0f};         // ground point under the feet (m)
    f32 yaw_deg = 0.0f;                    // facing; 0 looks along -Z
    f32 height_scale = 1.0f;               // 1.0 = 1.8 m
    f32 width_scale = 1.0f;                // thickness of torso and limbs
    vec3 skin_color{0.93f, 0.76f, 0.62f};  // sRGB, 0-1
    vec3 shirt_color{0.20f, 0.45f, 0.75f}; // upper body and sleeves
    vec3 pants_color{0.25f, 0.22f, 0.18f}; // hips and legs (shoes are a darker shade)
    bool ball_head = true;                 // Head uses the Ball shape

    static AvatarDesc preset(AvatarPreset p);
};

// Motor6D names of the rig. The animation clips address joints by these names.
inline constexpr std::array<const char*, 15> kJointNames = {
    "RootJoint", "Waist",    "Neck",    "RightShoulder", "LeftShoulder", "RightElbow", "LeftElbow", "RightWrist",
    "LeftWrist", "RightHip", "LeftHip", "RightKnee",     "LeftKnee",     "RightAnkle", "LeftAnkle",
};

// Height of the HumanoidRootPart centre above the ground in the rest pose (m). Set as Humanoid.HipHeight.
f32 r15_hip_height(f32 height_scale);

// Builds the avatar as a Model under `parent` (nullptr: detached). Returns the Model.
world::Instance* build_r15_avatar(world::DataModel& dm, world::Instance* parent, const AvatarDesc& desc);

// Imported character (spec M3b): a Model with an invisible HumanoidRootPart (the controller's capsule, no collision),
// a skinned MeshPart that shows the rigged .glb, a Humanoid (RigType Custom) and an Animator. The height comes from the
// file's bind-pose bounds times `scale`; HipHeight is half of it.
struct SkinnedAvatarDesc {
    std::string name = "Character";
    std::string mesh_path;         // .gltf/.glb with a skin (absolute or as the caller resolved it)
    f32 scale = 1.0f;              // uniform scale of the mesh (the MeshPart Size)
    vec3 origin{0.0f, 0.0f, 0.0f}; // ground point under the feet (m)
    f32 yaw_deg = 0.0f;
};
// False when the file cannot be read or has no skin (nothing is created then).
world::Instance* build_skinned_avatar(world::DataModel& dm, world::Instance* parent, const SkinnedAvatarDesc& desc);

} // namespace tefra::anim
