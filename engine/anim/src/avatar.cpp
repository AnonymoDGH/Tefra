#include "tefra/anim/avatar.h"

#include "tefra/anim/skeleton.h"

#include <vector>

namespace tefra::anim {

namespace {

// Rest geometry, unscaled metres. The feet touch y = 0 and the root centre sits at y = 0.89 (the pelvis).
constexpr f32 kRootY = 0.89f;

enum class Look : u8 { Root, Skin, Shirt, Pants, Shoe };

struct PartSpec {
    std::string name;
    vec3 center;
    vec3 size;
    Look look;
};

struct JointSpec {
    std::string name;
    std::string parent;
    std::string child;
    vec3 pivot; // joint position at rest (m)
};

struct Rig {
    std::vector<PartSpec> parts;
    std::vector<JointSpec> joints;
};

Rig make_rig() {
    Rig r;
    r.parts.push_back({"HumanoidRootPart", {0.0f, kRootY, 0.0f}, {0.50f, 0.90f, 0.35f}, Look::Root});
    r.parts.push_back({"LowerTorso", {0.0f, kRootY, 0.0f}, {0.42f, 0.22f, 0.24f}, Look::Pants});
    r.parts.push_back({"UpperTorso", {0.0f, 1.22f, 0.0f}, {0.52f, 0.44f, 0.26f}, Look::Shirt});
    r.parts.push_back({"Head", {0.0f, 1.62f, 0.0f}, {0.34f, 0.36f, 0.34f}, Look::Skin});
    r.joints.push_back({"RootJoint", "HumanoidRootPart", "LowerTorso", {0.0f, kRootY, 0.0f}});
    r.joints.push_back({"Waist", "LowerTorso", "UpperTorso", {0.0f, 1.00f, 0.0f}});
    r.joints.push_back({"Neck", "UpperTorso", "Head", {0.0f, 1.44f, 0.0f}});

    for (int k = 0; k < 2; ++k) {
        const f32 sx = k == 0 ? 1.0f : -1.0f;
        const std::string s = k == 0 ? "Right" : "Left";
        r.parts.push_back({s + "UpperArm", {sx * 0.30f, 1.23f, 0.0f}, {0.16f, 0.30f, 0.16f}, Look::Shirt});
        r.parts.push_back({s + "LowerArm", {sx * 0.30f, 0.94f, 0.0f}, {0.14f, 0.28f, 0.14f}, Look::Shirt});
        r.parts.push_back({s + "Hand", {sx * 0.30f, 0.72f, 0.0f}, {0.14f, 0.16f, 0.14f}, Look::Skin});
        r.parts.push_back({s + "UpperLeg", {sx * 0.11f, 0.61f, 0.0f}, {0.22f, 0.34f, 0.24f}, Look::Pants});
        r.parts.push_back({s + "LowerLeg", {sx * 0.11f, 0.28f, 0.0f}, {0.20f, 0.32f, 0.20f}, Look::Pants});
        r.parts.push_back({s + "Foot", {sx * 0.11f, 0.06f, -0.06f}, {0.22f, 0.12f, 0.40f}, Look::Shoe});

        r.joints.push_back({s + "Shoulder", "UpperTorso", s + "UpperArm", {sx * 0.30f, 1.38f, 0.0f}});
        r.joints.push_back({s + "Elbow", s + "UpperArm", s + "LowerArm", {sx * 0.30f, 1.08f, 0.0f}});
        r.joints.push_back({s + "Wrist", s + "LowerArm", s + "Hand", {sx * 0.30f, 0.80f, 0.0f}});
        r.joints.push_back({s + "Hip", "LowerTorso", s + "UpperLeg", {sx * 0.11f, 0.78f, 0.0f}});
        r.joints.push_back({s + "Knee", s + "UpperLeg", s + "LowerLeg", {sx * 0.11f, 0.44f, 0.0f}});
        r.joints.push_back({s + "Ankle", s + "LowerLeg", s + "Foot", {sx * 0.11f, 0.12f, 0.0f}});
    }
    return r;
}

f32 clamp_scale(f32 s) { return clamp(s, 0.5f, 1.5f); }

int index_of(const std::vector<PartSpec>& parts, const std::string& name) {
    for (usize i = 0; i < parts.size(); ++i)
        if (parts[i].name == name) return static_cast<int>(i);
    return -1;
}

} // namespace

AvatarDesc AvatarDesc::preset(AvatarPreset p) {
    AvatarDesc d;
    switch (p) {
    case AvatarPreset::Explorer:
        d.skin_color = {0.93f, 0.76f, 0.62f};
        d.shirt_color = {0.20f, 0.45f, 0.75f};
        d.pants_color = {0.25f, 0.22f, 0.18f};
        break;
    case AvatarPreset::Stocky:
        d.height_scale = 0.92f;
        d.width_scale = 1.18f;
        d.skin_color = {0.55f, 0.38f, 0.27f};
        d.shirt_color = {0.85f, 0.55f, 0.15f};
        d.pants_color = {0.18f, 0.18f, 0.22f};
        break;
    case AvatarPreset::Lanky:
        d.height_scale = 1.08f;
        d.width_scale = 0.90f;
        d.skin_color = {0.80f, 0.62f, 0.48f};
        d.shirt_color = {0.35f, 0.62f, 0.35f};
        d.pants_color = {0.30f, 0.26f, 0.42f};
        break;
    }
    return d;
}

f32 r15_hip_height(f32 height_scale) { return kRootY * clamp_scale(height_scale); }

world::Instance* build_r15_avatar(world::DataModel& dm, world::Instance* parent, const AvatarDesc& desc) {
    using world::Value;
    const f32 h = clamp_scale(desc.height_scale);
    const f32 w = clamp_scale(desc.width_scale);
    const Rig rig = make_rig();

    // Rest geometry is authored unscaled; S() applies the height (Y) and width (X, Z) scales.
    auto S = [&](vec3 v) { return vec3{v.x * w, v.y * h, v.z * w}; };
    const vec3 root_centre = S({0.0f, kRootY, 0.0f});
    const quat yaw = world::orientation_to_quat({0.0f, desc.yaw_deg, 0.0f});
    const vec3 root_world = desc.origin + vec3{0.0f, root_centre.y, 0.0f};
    // Rest position of a point given in the rig's unscaled model space (the root frame is yawed, not tilted).
    auto world_of = [&](vec3 model) { return root_world + rotate(yaw, S(model) - root_centre); };

    world::Instance* model = dm.create("Model", parent, desc.name);
    if (!model) return nullptr;

    std::vector<world::Instance*> parts;
    parts.reserve(rig.parts.size());
    const vec3 pants_shoe = desc.pants_color * 0.35f;
    for (const PartSpec& spec : rig.parts) {
        world::Instance* p = dm.create("Part", model, spec.name);
        if (!p) return nullptr;
        parts.push_back(p);
        const bool root = spec.look == Look::Root;
        p->set("Size", Value::vector3(S(spec.size)));
        p->set("Position", Value::vector3(world_of(spec.center)));
        p->set("Orientation", Value::vector3({0.0f, desc.yaw_deg, 0.0f}));
        vec3 color = desc.shirt_color;
        u32 material = 19; // Fabric
        switch (spec.look) {
        case Look::Root: color = {0.64f, 0.64f, 0.64f}; break;
        case Look::Skin:
            color = desc.skin_color;
            material = 1; // SmoothPlastic
            break;
        case Look::Shirt: color = desc.shirt_color; break;
        case Look::Pants: color = desc.pants_color; break;
        case Look::Shoe:
            color = pants_shoe;
            material = 0; // Plastic
            break;
        }
        p->set("Color", Value::color3(color));
        p->set("Material", Value::enumeration(material));
        p->set("CanCollide", Value::boolean(root)); // only the physical root collides
        p->set("Anchored", Value::boolean(false));
        if (root) p->set("Transparency", Value::number(1.0));
        if (spec.look == Look::Skin && spec.name == "Head" && desc.ball_head) p->set("Shape", Value::enumeration(1));
    }

    // Motor6D joints live under their parent part, as in the usual rig layout.
    for (const JointSpec& j : rig.joints) {
        const int pi = index_of(rig.parts, j.parent);
        const int ci = index_of(rig.parts, j.child);
        if (pi < 0 || ci < 0) return nullptr;
        const vec3 jl = S(j.pivot) - root_centre;
        const vec3 pl = S(rig.parts[usize(pi)].center) - root_centre;
        const vec3 cl = S(rig.parts[usize(ci)].center) - root_centre;
        // Rest: C0 = inverse(P) * J and C1 = inverse(C) * J, with all rest rotations equal to the root's.
        world::Instance* m = dm.create("Motor6D", parts[usize(pi)], j.name);
        if (!m) return nullptr;
        m->set("Part0", Value::reference(parts[usize(pi)]->id()));
        m->set("Part1", Value::reference(parts[usize(ci)]->id()));
        m->set("C0", Value::vector3(jl - pl));
        m->set("C1", Value::vector3(jl - cl));
    }

    world::Instance* hrp = parts[0];
    world::Instance* humanoid = dm.create("Humanoid", model, "Humanoid");
    if (!humanoid) return nullptr;
    humanoid->set("DisplayName", Value::string(desc.name));
    humanoid->set("HipHeight", Value::number(root_centre.y));
    humanoid->set("RigType", Value::enumeration(0)); // R15
    dm.create("Animator", humanoid, "Animator");
    model->set("PrimaryPart", Value::reference(hrp->id()));
    return model;
}

world::Instance* build_skinned_avatar(world::DataModel& dm, world::Instance* parent, const SkinnedAvatarDesc& desc) {
    using world::Value;
    SkeletonAsset asset;
    if (desc.mesh_path.empty() || !load_skeleton_asset(desc.mesh_path.c_str(), asset) || !asset.ok) return nullptr;
    const f32 scale = clamp(desc.scale, 0.01f, 100.0f);
    const f32 height = max(0.05f, (asset.bounds_max.y - asset.bounds_min.y) * scale);
    const f32 hip = height * 0.5f;
    const vec3 root_size{height * 0.30f, height * 0.50f, height * 0.25f};
    // Same convention as the controller: the root centre is HipHeight + Size.y / 2 above the feet.
    const f32 root_centre = hip + root_size.y * 0.5f;
    const vec3 root_pos = desc.origin + vec3{0.0f, root_centre, 0.0f};
    const vec3 yaw{0.0f, desc.yaw_deg, 0.0f};

    world::Instance* model = dm.create("Model", parent, desc.name);
    if (!model) return nullptr;
    world::Instance* hrp = dm.create("Part", model, "HumanoidRootPart");
    if (!hrp) return nullptr;
    hrp->set("Size", Value::vector3(root_size));
    hrp->set("Position", Value::vector3(root_pos));
    hrp->set("Orientation", Value::vector3(yaw));
    hrp->set("Transparency", Value::number(1.0));
    hrp->set("CanCollide", Value::boolean(false));
    hrp->set("Anchored", Value::boolean(false));

    // glTF models face +Z; the mesh is turned around so that it faces the same way as the R15 avatar (-Z).
    world::Instance* body = dm.create("MeshPart", model, "Body");
    if (!body) return nullptr;
    body->set("MeshId", Value::string(desc.mesh_path));
    body->set("Size", Value::vector3({scale, scale, scale}));
    body->set("Position", Value::vector3(root_pos - vec3{0.0f, root_centre, 0.0f}));
    body->set("Orientation", Value::vector3({0.0f, desc.yaw_deg + 180.0f, 0.0f}));
    body->set("CanCollide", Value::boolean(false));
    body->set("Anchored", Value::boolean(false));

    world::Instance* humanoid = dm.create("Humanoid", model, "Humanoid");
    if (!humanoid) return nullptr;
    humanoid->set("DisplayName", Value::string(desc.name));
    humanoid->set("HipHeight", Value::number(hip));
    humanoid->set("RigType", Value::enumeration(1)); // Custom
    dm.create("Animator", humanoid, "Animator");
    model->set("PrimaryPart", Value::reference(hrp->id()));
    return model;
}

} // namespace tefra::anim
