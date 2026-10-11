#pragma once

// Keyframe animation clips (spec M3, ADR-016). A clip holds one track per joint it animates; joints without a
// track keep the pose of the layer below. Clips are plain data: they load and save as .tefraanim (JSON).

#include "tefra/anim/transform.h"
#include "tefra/core/json_value.h"
#include "tefra/core/math.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::anim {

// Curve from one keyframe to the next.
enum class Easing : u8 { Linear, Smooth, Cubic, Step };

struct Keyframe {
    f32 time = 0.0f; // seconds from the clip start
    quat rot{};      // joint rotation (in the joint's frame)
    vec3 pos{};      // joint translation (m), usually zero except for the root bob
    Easing easing = Easing::Smooth;
};

struct JointTrack {
    std::string joint;          // Motor6D name, e.g. "RightElbow"
    std::vector<Keyframe> keys; // sorted by time
};

struct AnimationClip {
    std::string name;
    f32 duration = 0.0f; // time of the last key (s); recomputed by finalize()
    bool looped = true;  // wraps time; otherwise holds the last pose
    i32 priority = 0;    // layer: 0 locomotion, 1 airborne, 2+ actions and emotes
    std::vector<JointTrack> tracks;

    const JointTrack* track(std::string_view joint) const;
    // Local transform of one track at time t (wrapped or clamped according to `looped`).
    CFrame sample(const JointTrack& t, f32 time) const;
    // False when the clip does not animate this joint.
    bool sample_joint(std::string_view joint, f32 time, CFrame& out) const;
    void finalize(); // duration = largest key time

    JsonValue to_json() const;
    static bool from_json(const JsonValue& j, AnimationClip& out, std::string* error = nullptr);
    bool save(const char* path, std::string* error = nullptr) const;
    static bool load(const char* path, AnimationClip& out, std::string* error = nullptr);
};

using ClipPtr = std::shared_ptr<const AnimationClip>;

// Built-in clips, authored in code: idle, walk, run, jump, fall, climb and wave. nullptr for unknown names.
ClipPtr builtin_clip(std::string_view name);
std::vector<std::string> builtin_clip_names();

} // namespace tefra::anim
