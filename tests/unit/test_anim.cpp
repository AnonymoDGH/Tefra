#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/anim/animator.h"
#include "tefra/anim/avatar.h"
#include "tefra/anim/clip.h"
#include "tefra/anim/transform.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace tefra;
using namespace tefra::anim;
using namespace tefra::world;

namespace {

Instance* rig_part(Instance* model, const char* name) { return model->find_first_child(name); }

CFrame frame_of(const Instance* p) {
    return {p->get_vec3("Position"), orientation_to_quat(p->get_vec3("Orientation"))};
}

// Angle between two rotations, in degrees. Uses the relative rotation with atan2, which stays accurate for
// nearly equal quaternions (acos of a dot product near 1 is noisy in single precision).
f32 angle_deg(quat a, quat b) {
    const quat r = conjugate(a) * b;
    return degrees(2.0f * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::fabs(r.w)));
}

f32 max_angle_between(const CFrame& a, const CFrame& b) { return angle_deg(a.q, b.q); }

usize count_class(const Instance* root, const char* cls) {
    usize n = 0;
    for (const Instance* d : root->descendants())
        if (d->is_a(cls)) ++n;
    return n;
}

} // namespace

TEST_CASE("avatar: 15 body parts plus the root, 15 Motor6D joints, Humanoid and Animator") {
    DataModel dm;
    Instance* model = build_r15_avatar(dm, dm.workspace(), AvatarDesc::preset(AvatarPreset::Explorer));
    REQUIRE(model != nullptr);
    CHECK(model->name() == "Character");
    CHECK(model->get("PrimaryPart") != nullptr);

    usize parts = 0;
    for (const Instance* c : model->children())
        if (c->is_a("BasePart")) ++parts;
    CHECK(parts == 16);                         // HumanoidRootPart + 15 body parts
    CHECK(count_class(model, "Motor6D") == 15); // one per joint, RootJoint included

    Instance* hum = model->find_first_child("Humanoid");
    REQUIRE(hum != nullptr);
    REQUIRE(hum->find_first_child_of_class("Animator") != nullptr);
    CHECK(hum->get_float("HipHeight") > 0.3f);
    CHECK(hum->get_float("HipHeight") < 0.5f);

    // Every joint name of the rig exists and connects two parts of the model.
    for (const char* joint : kJointNames) {
        Instance* m = nullptr;
        for (Instance* d : model->descendants())
            if (d->is_a("Motor6D") && d->name() == joint) m = d;
        REQUIRE(m != nullptr);
        CHECK(dm.find(m->get("Part0")->ref) != nullptr);
        CHECK(dm.find(m->get("Part1")->ref) != nullptr);
    }
    // Only the physical root collides; the root is invisible.
    CHECK(rig_part(model, "HumanoidRootPart")->get_bool("CanCollide"));
    CHECK_FALSE(rig_part(model, "UpperTorso")->get_bool("CanCollide"));
    CHECK(rig_part(model, "HumanoidRootPart")->get_float("Transparency") == doctest::Approx(1.0f));
    // 1.75 m tall for the default proportions.
    const Instance* head = rig_part(model, "Head");
    const f32 top = head->get_vec3("Position").y + head->get_vec3("Size").y * 0.5f;
    CHECK(top == doctest::Approx(1.75f).epsilon(0.01));
}

TEST_CASE("avatar: rest pose closes every joint (Part1 = Part0 * C0 * inverse(C1))") {
    for (AvatarPreset p : {AvatarPreset::Explorer, AvatarPreset::Stocky, AvatarPreset::Lanky}) {
        DataModel dm;
        AvatarDesc d = AvatarDesc::preset(p);
        d.yaw_deg = 35.0f;
        d.origin = {4.0f, 0.5f, -7.0f};
        Instance* model = build_r15_avatar(dm, dm.workspace(), d);
        REQUIRE(model != nullptr);
        for (Instance* m : model->descendants()) {
            if (!m->is_a("Motor6D")) continue;
            const CFrame p0 = frame_of(dm.find(m->get("Part0")->ref));
            const CFrame c0{m->get_vec3("C0"), orientation_to_quat(m->get_vec3("C0Orientation"))};
            const CFrame c1{m->get_vec3("C1"), orientation_to_quat(m->get_vec3("C1Orientation"))};
            const CFrame expected = p0 * c0 * inverse(c1);
            const CFrame actual = frame_of(dm.find(m->get("Part1")->ref));
            CHECK(length(expected.p - actual.p) < 1e-4f);
            CHECK(max_angle_between(expected, actual) < 0.01f);
        }
    }
}

TEST_CASE("avatar: soles on the ground when the root is HipHeight + Size.y / 2 up") {
    for (AvatarPreset p : {AvatarPreset::Explorer, AvatarPreset::Stocky, AvatarPreset::Lanky}) {
        DataModel dm;
        const AvatarDesc d = AvatarDesc::preset(p);
        Instance* model = build_r15_avatar(dm, dm.workspace(), d);
        REQUIRE(model != nullptr);
        Instance* hum = model->find_first_child("Humanoid");
        Instance* root = rig_part(model, "HumanoidRootPart");
        // Controller convention: the root centre sits HipHeight + Size.y / 2 above the ground (y = 0).
        const f32 centre = hum->get_float("HipHeight") + root->get_vec3("Size").y * 0.5f;
        root->set("Position", Value::vector3({0.0f, centre, 0.0f}));
        CHECK(hum->get_float("HipHeight") == doctest::Approx(r15_hip_height(d.height_scale)));

        // One idle frame: the breathing touches the torso and arms only, so the feet must stay on the ground.
        AnimatorSystem animator(dm);
        animator.update(1.0f / 60.0f);

        f32 lowest_sole = 1e9f;
        for (Instance* part : model->children()) {
            if (!part->is_a("BasePart") || part == root) continue;
            const f32 bottom = part->get_vec3("Position").y - part->get_vec3("Size").y * 0.5f;
            if (part->name() == "LeftFoot" || part->name() == "RightFoot") lowest_sole = std::min(lowest_sole, bottom);
            CHECK(bottom > -1e-3f); // nothing below the ground
        }
        CHECK(lowest_sole == doctest::Approx(0.0f).epsilon(1e-3));
    }
}

TEST_CASE("clip: walk is periodic, pose(t) == pose(t + 1 s)") {
    const ClipPtr walk = builtin_clip("walk");
    REQUIRE(walk != nullptr);
    CHECK(walk->duration == doctest::Approx(1.0f));
    CHECK(walk->looped);
    for (const JointTrack& t : walk->tracks) {
        for (f32 time : {0.0f, 0.13f, 0.37f, 0.61f, 0.88f}) {
            const CFrame a = walk->sample(t, time);
            const CFrame b = walk->sample(t, time + 1.0f);
            CHECK(length(a.p - b.p) < 1e-5f);
            CHECK(angle_deg(a.q, b.q) < 0.01f);
        }
    }
    // The cycle really moves: the right hip swings by tens of degrees.
    const JointTrack* hip = walk->track("RightHip");
    REQUIRE(hip != nullptr);
    CHECK(angle_deg(walk->sample(*hip, 0.0f).q, walk->sample(*hip, 0.625f).q) > 20.0f);
}

TEST_CASE("animator: crossfade from idle to walk is continuous") {
    DataModel dm;
    Instance* model = build_r15_avatar(dm, dm.workspace(), AvatarDesc{});
    REQUIRE(model != nullptr);
    Instance* hum = model->find_first_child("Humanoid");
    Instance* root = rig_part(model, "HumanoidRootPart");
    AnimatorSystem animator(dm);
    const f32 dt = 1.0f / 120.0f;

    for (int i = 0; i < 60; ++i) animator.update(dt); // half a second standing still
    CHECK(animator.is_playing(*hum, "idle"));
    CHECK_FALSE(animator.is_playing(*hum, "walk"));

    root->set("AssemblyLinearVelocity", Value::vector3({0.0f, 0.0f, 1.4f}));
    const char* watched[] = {"RightUpperLeg", "RightUpperArm", "LeftUpperArm", "Head"};
    CFrame prev[4];
    for (usize k = 0; k < 4; ++k) prev[k] = frame_of(rig_part(model, watched[k]));
    f32 max_step_deg = 0.0f;
    f32 max_step_m = 0.0f;
    for (int i = 0; i < 60; ++i) { // 0.5 s, the fade lasts 0.2 s
        animator.update(dt);
        for (usize k = 0; k < 4; ++k) {
            const CFrame cur = frame_of(rig_part(model, watched[k]));
            max_step_deg = std::max(max_step_deg, angle_deg(prev[k].q, cur.q));
            max_step_m = std::max(max_step_m, length(cur.p - prev[k].p));
            prev[k] = cur;
        }
    }
    // A pop would move a limb by tens of degrees in one frame; the gait itself stays under about 5 degrees.
    CHECK(max_step_deg < 6.0f);
    CHECK(max_step_m < 0.06f);
    CHECK(max_step_deg > 0.05f); // it did move
    CHECK(animator.is_playing(*hum, "walk"));
    CHECK_FALSE(animator.is_playing(*hum, "idle"));
}

TEST_CASE("animator: manual clip on the arm layer leaves the legs on the gait") {
    DataModel dm;
    Instance* model = build_r15_avatar(dm, dm.workspace(), AvatarDesc{});
    REQUIRE(model != nullptr);
    Instance* hum = model->find_first_child("Humanoid");
    Instance* root = rig_part(model, "HumanoidRootPart");
    AnimatorSystem animator(dm);
    const f32 dt = 1.0f / 120.0f;
    root->set("AssemblyLinearVelocity", Value::vector3({0.0f, 0.0f, 1.4f}));
    for (int i = 0; i < 60; ++i) animator.update(dt);

    const f32 rest_arm_pitch = angle_deg(frame_of(rig_part(model, "RightUpperArm")).q, quat{});
    CHECK(animator.play(*hum, "wave", 0.2f));
    CHECK(animator.is_playing(*hum, "wave"));
    for (int i = 0; i < 72; ++i) animator.update(dt); // 0.6 s: the arm is up
    const f32 raised = angle_deg(frame_of(rig_part(model, "RightUpperArm")).q, quat{});
    CHECK(raised > 60.0f);
    CHECK(raised > rest_arm_pitch + 30.0f);
    // The left arm still swings with the walk.
    const CFrame left_a = frame_of(rig_part(model, "LeftUpperArm"));
    for (int i = 0; i < 12; ++i) animator.update(dt);
    CHECK(angle_deg(left_a.q, frame_of(rig_part(model, "LeftUpperArm")).q) > 0.5f);

    animator.stop(*hum, "wave", 0.1f);
    for (int i = 0; i < 60; ++i) animator.update(dt);
    CHECK_FALSE(animator.is_playing(*hum, "wave"));
    CHECK(angle_deg(frame_of(rig_part(model, "RightUpperArm")).q, quat{}) < 40.0f);
}

TEST_CASE("animator: jump and fall follow Humanoid.State") {
    DataModel dm;
    Instance* model = build_r15_avatar(dm, dm.workspace(), AvatarDesc{});
    REQUIRE(model != nullptr);
    Instance* hum = model->find_first_child("Humanoid");
    AnimatorSystem animator(dm);
    const f32 dt = 1.0f / 120.0f;

    hum->set("State", Value::enumeration(3), true); // Jumping
    for (int i = 0; i < 12; ++i) animator.update(dt);
    CHECK(animator.is_playing(*hum, "jump"));
    CHECK_FALSE(animator.is_playing(*hum, "fall"));

    hum->set("State", Value::enumeration(4), true); // Freefall
    for (int i = 0; i < 60; ++i) animator.update(dt);
    CHECK(animator.is_playing(*hum, "fall"));
    CHECK_FALSE(animator.is_playing(*hum, "jump"));

    hum->set("State", Value::enumeration(0), true); // Idle on the ground again
    for (int i = 0; i < 60; ++i) animator.update(dt);
    CHECK(animator.is_playing(*hum, "idle"));
    CHECK_FALSE(animator.is_playing(*hum, "fall"));
}

TEST_CASE("clip: JSON round trip keeps keys, easing and poses; bad files are rejected") {
    for (const char* name : {"walk", "wave", "jump", "idle"}) {
        const ClipPtr c = builtin_clip(name);
        REQUIRE(c != nullptr);
        JsonValue parsed;
        REQUIRE(JsonValue::parse(c->to_json().dump(true), parsed));
        AnimationClip r;
        std::string error;
        REQUIRE(AnimationClip::from_json(parsed, r, &error));
        CHECK(r.name == c->name);
        CHECK(r.looped == c->looped);
        CHECK(r.priority == c->priority);
        CHECK(r.duration == doctest::Approx(c->duration));
        REQUIRE(r.tracks.size() == c->tracks.size());
        for (usize i = 0; i < c->tracks.size(); ++i) {
            const JointTrack& a = c->tracks[i];
            const JointTrack& b = r.tracks[i];
            CHECK(a.joint == b.joint);
            REQUIRE(a.keys.size() == b.keys.size());
            for (usize k = 0; k < a.keys.size(); ++k) {
                CHECK(a.keys[k].time == doctest::Approx(b.keys[k].time));
                CHECK(a.keys[k].easing == b.keys[k].easing);
                CHECK(angle_deg(a.keys[k].rot, b.keys[k].rot) < 0.001f);
            }
            for (f32 time : {0.0f, 0.3f, 0.9f, 1.7f}) {
                const CFrame sa = c->sample(a, time);
                const CFrame sb = r.sample(b, time);
                CHECK(length(sa.p - sb.p) < 1e-5f);
                CHECK(angle_deg(sa.q, sb.q) < 0.001f);
            }
        }
    }
    AnimationClip out;
    std::string error;
    JsonValue bad;
    REQUIRE(JsonValue::parse(R"({"format":"something-else","tracks":[]})", bad));
    CHECK_FALSE(AnimationClip::from_json(bad, out, &error));
    CHECK_FALSE(error.empty());
}
