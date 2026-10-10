// Built-in clips, authored by hand (spec M3 ADR-016: procedural animations, no third-party data).
//
// Conventions: the character faces -Z and its right side is +X. Rotations are given as (pitch about X, yaw about Y,
// roll about Z) in degrees, the same order as Part.Orientation. Positive pitch swings a hanging limb forward (-Z),
// negative pitch on the waist leans the torso forward, positive roll on the right arm lifts it away from the body.
// Looped clips repeat their first key at the end time, so pose(t) == pose(t + duration).

#include "tefra/anim/clip.h"

#include "tefra/world/datamodel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tefra::anim {

namespace {

// Keyframe from a designed pose (degrees).
Keyframe K(f32 t, f32 pitch, f32 yaw, f32 roll, Easing e = Easing::Smooth) {
    Keyframe k;
    k.time = t;
    k.rot = world::orientation_to_quat(vec3{pitch, yaw, roll});
    k.easing = e;
    return k;
}

// Keyframe with a vertical translation only (root bob, crouch).
Keyframe KY(f32 t, f32 y, Easing e = Easing::Smooth) {
    Keyframe k = K(t, 0.0f, 0.0f, 0.0f, e);
    k.pos = {0.0f, y, 0.0f};
    return k;
}

void track(AnimationClip& c, const char* joint, std::vector<Keyframe> keys) {
    JointTrack t;
    t.joint = joint;
    t.keys = std::move(keys);
    c.tracks.push_back(std::move(t));
}

// Adds the closing key of a periodic track (same pose as the first key, at the clip end).
void close_loop(std::vector<Keyframe>& keys, f32 duration) {
    if (keys.empty()) return;
    Keyframe end = keys.front();
    end.time = duration;
    keys.push_back(end);
}

// ---- Locomotion -----------------------------------------------------------------------------------------------

// Right-leg gait over one cycle, designed as five key poses (hip, knee, ankle in degrees, phase in [0, 1)):
// contact, mid-stance, push-off, swing peak, reach. The left leg runs the same table half a cycle later.
struct GaitRow {
    f32 t, hip, knee, ankle;
};
constexpr std::array<GaitRow, 5> kGait = {{
    {0.00f, 24.0f, -4.0f, -6.0f},
    {0.25f, 2.0f, -8.0f, 2.0f},
    {0.50f, -16.0f, -18.0f, 14.0f},
    {0.625f, -4.0f, -60.0f, 6.0f},
    {0.80f, 14.0f, -28.0f, -4.0f},
}};

struct GaitStyle {
    f32 duration; // seconds per cycle
    f32 leg;      // multiplier on the hip, knee and ankle amplitudes
    f32 arm_swing;
    f32 elbow;    // constant elbow flexion (degrees)
    f32 lean;     // forward lean of the waist (degrees)
    f32 twist;    // torso yaw counter to the legs (degrees)
    f32 bob;      // pelvis height change over the cycle (m)
    f32 arm_roll; // arms held away from the body (degrees)
};

void add_gait(AnimationClip& c, const GaitStyle& g) {
    const f32 D = g.duration;
    for (int side = 0; side < 2; ++side) { // 0 right, 1 left
        const f32 shift = side ? 0.5f : 0.0f;
        const f32 roll = side ? -g.arm_roll : g.arm_roll;
        std::array<GaitRow, kGait.size()> rows = kGait;
        for (GaitRow& r : rows) r.t = std::fmod(r.t + shift, 1.0f);
        std::sort(rows.begin(), rows.end(), [](const GaitRow& a, const GaitRow& b) { return a.t < b.t; });

        std::vector<Keyframe> hip, knee, ankle, shoulder, elbow;
        for (const GaitRow& r : rows) {
            const f32 t = r.t * D;
            hip.push_back(K(t, r.hip * g.leg, 0.0f, 0.0f));
            knee.push_back(K(t, r.knee * g.leg, 0.0f, 0.0f));
            ankle.push_back(K(t, r.ankle * g.leg, 0.0f, 0.0f));
            // The arm swings opposite to the leg on the same side.
            shoulder.push_back(K(t, -r.hip * g.leg * g.arm_swing, 0.0f, roll));
        }
        elbow.push_back(K(0.0f, g.elbow, 0.0f, 0.0f));
        elbow.push_back(K(D, g.elbow, 0.0f, 0.0f));
        close_loop(hip, D);
        close_loop(knee, D);
        close_loop(ankle, D);
        close_loop(shoulder, D);

        track(c, side ? "LeftHip" : "RightHip", std::move(hip));
        track(c, side ? "LeftKnee" : "RightKnee", std::move(knee));
        track(c, side ? "LeftAnkle" : "RightAnkle", std::move(ankle));
        track(c, side ? "LeftShoulder" : "RightShoulder", std::move(shoulder));
        track(c, side ? "LeftElbow" : "RightElbow", std::move(elbow));
    }
    // Torso counter-rotates to the legs; the head keeps looking forward. Pelvis bobs once per step.
    track(c, "Waist",
          {K(0.0f, -g.lean, -g.twist, 0.0f), K(D * 0.5f, -g.lean, g.twist, 0.0f), K(D, -g.lean, -g.twist, 0.0f)});
    track(c, "Neck",
          {K(0.0f, g.lean, g.twist * 0.5f, 0.0f), K(D * 0.5f, g.lean, -g.twist * 0.5f, 0.0f),
           K(D, g.lean, g.twist * 0.5f, 0.0f)});
    track(c, "RootJoint",
          {KY(0.0f, -g.bob), KY(D * 0.25f, g.bob), KY(D * 0.5f, -g.bob), KY(D * 0.75f, g.bob), KY(D, -g.bob)});
}

AnimationClip make_gait(const char* name, const GaitStyle& g) {
    AnimationClip c;
    c.name = name;
    c.looped = true;
    c.priority = 0;
    add_gait(c, g);
    c.finalize();
    return c;
}

// ---- Idle --------------------------------------------------------------------------------------------------------

AnimationClip make_idle() {
    AnimationClip c;
    c.name = "idle";
    c.looped = true;
    c.priority = 0;
    // Breathing: the chest lifts and settles over 3.5 s; the arms hang relaxed with a slight bend at the elbow.
    track(c, "Waist", {K(0.0f, 0.0f, 0.0f, 0.0f), K(1.75f, 1.2f, 0.0f, 0.0f), K(3.5f, 0.0f, 0.0f, 0.0f)});
    track(c, "RightShoulder", {K(0.0f, 0.0f, 0.0f, 4.0f), K(1.75f, 1.0f, 0.0f, 5.0f), K(3.5f, 0.0f, 0.0f, 4.0f)});
    track(c, "LeftShoulder", {K(0.0f, 0.0f, 0.0f, -4.0f), K(1.75f, 1.0f, 0.0f, -5.0f), K(3.5f, 0.0f, 0.0f, -4.0f)});
    track(c, "RightElbow", {K(0.0f, 12.0f, 0.0f, 0.0f), K(3.5f, 12.0f, 0.0f, 0.0f)});
    track(c, "LeftElbow", {K(0.0f, 12.0f, 0.0f, 0.0f), K(3.5f, 12.0f, 0.0f, 0.0f)});
    c.finalize();
    return c;
}

// ---- Airborne ----------------------------------------------------------------------------------------------------

// Takeoff: crouch, push off with the toes, arms swing up. Not looped: it holds its last pose while the
// Humanoid stays in the Jumping state, and the Animator fades to the fall clip when it leaves.
AnimationClip make_jump() {
    AnimationClip c;
    c.name = "jump";
    c.looped = false;
    c.priority = 1;
    track(c, "RootJoint", {KY(0.0f, 0.0f), KY(0.15f, -0.05f), KY(0.30f, 0.0f), KY(0.60f, 0.0f)});
    track(c, "Waist",
          {K(0.0f, -6.0f, 0.0f, 0.0f), K(0.15f, -4.0f, 0.0f, 0.0f), K(0.35f, -2.0f, 0.0f, 0.0f),
           K(0.60f, -2.0f, 0.0f, 0.0f)});
    track(c, "RightHip",
          {K(0.0f, 20.0f, 0.0f, 0.0f), K(0.15f, 0.0f, 0.0f, 0.0f), K(0.35f, 45.0f, 0.0f, 0.0f),
           K(0.60f, 35.0f, 0.0f, 0.0f)});
    track(c, "LeftHip",
          {K(0.0f, 20.0f, 0.0f, 0.0f), K(0.15f, 0.0f, 0.0f, 0.0f), K(0.35f, 45.0f, 0.0f, 0.0f),
           K(0.60f, 35.0f, 0.0f, 0.0f)});
    track(c, "RightKnee",
          {K(0.0f, -30.0f, 0.0f, 0.0f), K(0.15f, -5.0f, 0.0f, 0.0f), K(0.35f, -60.0f, 0.0f, 0.0f),
           K(0.60f, -50.0f, 0.0f, 0.0f)});
    track(c, "LeftKnee",
          {K(0.0f, -30.0f, 0.0f, 0.0f), K(0.15f, -5.0f, 0.0f, 0.0f), K(0.35f, -60.0f, 0.0f, 0.0f),
           K(0.60f, -50.0f, 0.0f, 0.0f)});
    track(c, "RightAnkle", {K(0.0f, 12.0f, 0.0f, 0.0f), K(0.15f, -20.0f, 0.0f, 0.0f), K(0.35f, 0.0f, 0.0f, 0.0f)});
    track(c, "LeftAnkle", {K(0.0f, 12.0f, 0.0f, 0.0f), K(0.15f, -20.0f, 0.0f, 0.0f), K(0.35f, 0.0f, 0.0f, 0.0f)});
    track(c, "RightShoulder",
          {K(0.0f, -25.0f, 0.0f, 0.0f), K(0.15f, 150.0f, 0.0f, 14.0f), K(0.35f, 165.0f, 0.0f, 12.0f),
           K(0.60f, 160.0f, 0.0f, 12.0f)});
    track(c, "LeftShoulder",
          {K(0.0f, -25.0f, 0.0f, 0.0f), K(0.15f, 150.0f, 0.0f, -14.0f), K(0.35f, 165.0f, 0.0f, -12.0f),
           K(0.60f, 160.0f, 0.0f, -12.0f)});
    track(c, "RightElbow", {K(0.0f, 10.0f, 0.0f, 0.0f), K(0.15f, 5.0f, 0.0f, 0.0f), K(0.60f, 10.0f, 0.0f, 0.0f)});
    track(c, "LeftElbow", {K(0.0f, 10.0f, 0.0f, 0.0f), K(0.15f, 5.0f, 0.0f, 0.0f), K(0.60f, 10.0f, 0.0f, 0.0f)});
    c.finalize();
    return c;
}

// Freefall: arms up and out, a slight flutter, legs loose.
AnimationClip make_fall() {
    AnimationClip c;
    c.name = "fall";
    c.looped = true;
    c.priority = 1;
    track(c, "RightShoulder",
          {K(0.0f, 146.0f, 0.0f, 20.0f), K(0.6f, 154.0f, 0.0f, 20.0f), K(1.2f, 146.0f, 0.0f, 20.0f)});
    track(c, "LeftShoulder",
          {K(0.0f, 154.0f, 0.0f, -20.0f), K(0.6f, 146.0f, 0.0f, -20.0f), K(1.2f, 154.0f, 0.0f, -20.0f)});
    track(c, "RightElbow", {K(0.0f, 12.0f, 0.0f, 0.0f), K(0.6f, 20.0f, 0.0f, 0.0f), K(1.2f, 12.0f, 0.0f, 0.0f)});
    track(c, "LeftElbow", {K(0.0f, 20.0f, 0.0f, 0.0f), K(0.6f, 12.0f, 0.0f, 0.0f), K(1.2f, 20.0f, 0.0f, 0.0f)});
    track(c, "RightHip", {K(0.0f, 8.0f, 0.0f, 0.0f), K(1.2f, 8.0f, 0.0f, 0.0f)});
    track(c, "LeftHip", {K(0.0f, 8.0f, 0.0f, 0.0f), K(1.2f, 8.0f, 0.0f, 0.0f)});
    track(c, "RightKnee", {K(0.0f, -14.0f, 0.0f, 0.0f), K(1.2f, -14.0f, 0.0f, 0.0f)});
    track(c, "LeftKnee", {K(0.0f, -14.0f, 0.0f, 0.0f), K(1.2f, -14.0f, 0.0f, 0.0f)});
    track(c, "Waist", {K(0.0f, 4.0f, 0.0f, 0.0f), K(1.2f, 4.0f, 0.0f, 0.0f)});
    c.finalize();
    return c;
}

// Ladder or wall climb: hands reach up in turn while the knees drive up alternately.
AnimationClip make_climb() {
    AnimationClip c;
    c.name = "climb";
    c.looped = true;
    c.priority = 1;
    track(c, "RightShoulder", {K(0.0f, 160.0f, 0.0f, 8.0f), K(0.7f, 110.0f, 0.0f, 8.0f), K(1.4f, 160.0f, 0.0f, 8.0f)});
    track(c, "LeftShoulder",
          {K(0.0f, 110.0f, 0.0f, -8.0f), K(0.7f, 160.0f, 0.0f, -8.0f), K(1.4f, 110.0f, 0.0f, -8.0f)});
    track(c, "RightElbow", {K(0.0f, 30.0f, 0.0f, 0.0f), K(0.7f, 80.0f, 0.0f, 0.0f), K(1.4f, 30.0f, 0.0f, 0.0f)});
    track(c, "LeftElbow", {K(0.0f, 80.0f, 0.0f, 0.0f), K(0.7f, 30.0f, 0.0f, 0.0f), K(1.4f, 80.0f, 0.0f, 0.0f)});
    track(c, "RightHip", {K(0.0f, 35.0f, 0.0f, 0.0f), K(0.7f, -5.0f, 0.0f, 0.0f), K(1.4f, 35.0f, 0.0f, 0.0f)});
    track(c, "RightKnee", {K(0.0f, -60.0f, 0.0f, 0.0f), K(0.7f, -10.0f, 0.0f, 0.0f), K(1.4f, -60.0f, 0.0f, 0.0f)});
    track(c, "LeftHip", {K(0.0f, -5.0f, 0.0f, 0.0f), K(0.7f, 35.0f, 0.0f, 0.0f), K(1.4f, -5.0f, 0.0f, 0.0f)});
    track(c, "LeftKnee", {K(0.0f, -10.0f, 0.0f, 0.0f), K(0.7f, -60.0f, 0.0f, 0.0f), K(1.4f, -10.0f, 0.0f, 0.0f)});
    track(c, "Waist", {K(0.0f, -8.0f, 0.0f, 0.0f), K(1.4f, -8.0f, 0.0f, 0.0f)});
    c.finalize();
    return c;
}

// Emote: the right arm goes up and waves three times, then comes down. One shot, priority 2: it replaces the
// right arm of the locomotion layer and leaves the legs alone.
AnimationClip make_wave() {
    AnimationClip c;
    c.name = "wave";
    c.looped = false;
    c.priority = 2;
    track(c, "RightShoulder",
          {K(0.0f, 0.0f, 0.0f, 0.0f), K(0.4f, 150.0f, 0.0f, 20.0f), K(2.0f, 150.0f, 0.0f, 20.0f),
           K(2.4f, 0.0f, 0.0f, 0.0f)});
    std::vector<Keyframe> elbow = {K(0.0f, 0.0f, 0.0f, 0.0f), K(0.4f, 70.0f, 0.0f, 0.0f)};
    std::vector<Keyframe> wrist = {K(0.4f, 0.0f, 0.0f, 0.0f)};
    for (int i = 0; i < 5; ++i) {
        const f32 t = 0.6f + 0.3f * static_cast<f32>(i);
        elbow.push_back(K(t, (i % 2) ? 70.0f : 95.0f, 0.0f, 0.0f));
    }
    for (int i = 0; i < 8; ++i) {
        const f32 t = 0.6f + 0.2f * static_cast<f32>(i);
        wrist.push_back(K(t, 0.0f, 0.0f, (i % 2) ? 20.0f : -20.0f));
    }
    elbow.push_back(K(2.0f, 80.0f, 0.0f, 0.0f));
    elbow.push_back(K(2.4f, 0.0f, 0.0f, 0.0f));
    wrist.push_back(K(2.4f, 0.0f, 0.0f, 0.0f));
    track(c, "RightElbow", std::move(elbow));
    track(c, "RightWrist", std::move(wrist));
    c.finalize();
    return c;
}

std::vector<ClipPtr> make_all() {
    std::vector<ClipPtr> all;
    all.push_back(std::make_shared<const AnimationClip>(make_idle()));
    all.push_back(std::make_shared<const AnimationClip>(
        make_gait("walk", GaitStyle{1.0f, 1.0f, 0.7f, 18.0f, 3.0f, 4.0f, 0.02f, 5.0f})));
    all.push_back(std::make_shared<const AnimationClip>(
        make_gait("run", GaitStyle{0.7f, 1.5f, 1.0f, 80.0f, 9.0f, 8.0f, 0.045f, 12.0f})));
    all.push_back(std::make_shared<const AnimationClip>(make_jump()));
    all.push_back(std::make_shared<const AnimationClip>(make_fall()));
    all.push_back(std::make_shared<const AnimationClip>(make_climb()));
    all.push_back(std::make_shared<const AnimationClip>(make_wave()));
    return all;
}

const std::vector<ClipPtr>& builtins() {
    static const std::vector<ClipPtr> all = make_all();
    return all;
}

} // namespace

ClipPtr builtin_clip(std::string_view name) {
    for (const ClipPtr& c : builtins())
        if (c->name == name) return c;
    return nullptr;
}

std::vector<std::string> builtin_clip_names() {
    std::vector<std::string> names;
    for (const ClipPtr& c : builtins()) names.push_back(c->name);
    return names;
}

} // namespace tefra::anim
