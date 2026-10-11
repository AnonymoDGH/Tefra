// CharacterController: Jolt CharacterVirtual driving a Humanoid's HumanoidRootPart (spec M3, ADR-014).
#include "internal.h"

#include "tefra/core/log.h"

#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include <cmath>

namespace tefra::physics {

using detail::from_jolt;
using detail::to_jolt;

namespace {

constexpr f32 kStepUpHeight = 0.4f;     // walks up steps this high (Jolt's default, stated explicitly)
constexpr f32 kMaxSlopeDegrees = 45.0f; // steeper ground is not walkable
constexpr f32 kLandedSeconds = 0.15f;   // Humanoid.State stays Landed this long after touchdown
constexpr f32 kMoveEpsilon = 0.01f;     // root part moved by someone else by more than this: teleport
constexpr f32 kMoveSpeedThreshold = 0.1f;
constexpr f32 kMinRadius = 0.2f;
constexpr f32 kMaxRadius = 1.0f;
constexpr f32 kCharacterMass = 80.0f;

// Index of a HumanoidState item (world/src/classes.cpp), 0 if the registry lacks it.
u32 state_index(const char* name) {
    const world::EnumInfo* e = world::ClassRegistry::get().find_enum("HumanoidState");
    const i32 i = e ? e->index_of(name) : -1;
    return i < 0 ? 0u : static_cast<u32>(i);
}

// Distance from the root part centre down to the feet.
f32 root_offset(const world::Instance& hum, const world::Instance& root) {
    return hum.get_float("HipHeight", 0.9f) + root.get_vec3("Size", {2.0f, 2.0f, 1.0f}).y * 0.5f;
}

vec3 feet_to_root(vec3 feet, const world::Instance& hum, const world::Instance& root) {
    return feet + vec3{0.0f, root_offset(hum, root), 0.0f};
}

} // namespace

namespace detail {

void character_pre_step(CharacterImpl& c, WorldImpl& w, f32 dt) {
    if (!c.cv || !c.world) return;
    world::Instance* model = w.model.find(c.model_id);
    world::Instance* root = w.model.find(c.root_id);
    world::Instance* hum = w.model.find(c.humanoid_id);
    if (!model || !root || !hum) return;

    // A script or the editor moved the root part: follow it.
    const vec3 root_pos = root->get_vec3("Position");
    if (length(root_pos - c.last_root_pos) > kMoveEpsilon) {
        c.cv->SetPosition(JPH::RVec3(to_jolt(root_pos - vec3{0.0f, root_offset(*hum, *root), 0.0f})));
        c.cv->SetLinearVelocity(JPH::Vec3::sZero());
    }

    const f32 g = w.gravity();
    const bool ground = c.cv->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    JPH::Vec3 v = c.cv->GetLinearVelocity();
    const f32 walk = hum->get_float("WalkSpeed", 6.0f);
    // On a moving part (a platform moved by a script, a kinematic body) the character must move with its ground: the
    // ground velocity is part of the desired velocity (Jolt's samples do the same).
    const JPH::Vec3 ground_v = ground ? c.cv->GetGroundVelocity() : JPH::Vec3::sZero();
    v.SetX(c.move_dir.x * walk + ground_v.GetX());
    v.SetZ(c.move_dir.z * walk + ground_v.GetZ());
    // CharacterVirtual does not integrate gravity itself: the caller adds it to the vertical velocity.
    if (ground) {
        if (c.jump_requested) {
            v.SetY(std::sqrt(2.0f * g * max(hum->get_float("JumpHeight", 1.6f), 0.0f)) + ground_v.GetY());
            c.jumping = true;
        } else {
            v.SetY(ground_v.GetY() < 0.0f ? ground_v.GetY() : max(ground_v.GetY(), 0.0f));
        }
    } else {
        v.SetY(v.GetY() - g * dt);
    }
    c.jump_requested = false;
    c.cv->SetLinearVelocity(v);

    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, kStepUpHeight, 0.0f);
    c.cv->ExtendedUpdate(dt, JPH::Vec3(0.0f, -g, 0.0f), settings,
                         w.system.GetDefaultBroadPhaseLayerFilter(kLayerDynamic),
                         w.system.GetDefaultLayerFilter(kLayerDynamic), {}, {}, w.temp);
}

void character_post_step(CharacterImpl& c, WorldImpl& w, f32 dt) {
    if (!c.cv || !c.world) return;
    world::Instance* model = w.model.find(c.model_id);
    world::Instance* root = w.model.find(c.root_id);
    world::Instance* hum = w.model.find(c.humanoid_id);
    if (!model || !root || !hum) return;

    c.velocity = from_jolt(c.cv->GetLinearVelocity());
    const bool grounded = c.cv->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    const vec3 feet = from_jolt(c.cv->GetPosition());
    const vec3 new_root = feet_to_root(feet, *hum, *root);
    // Walking speed for the animation: the motion relative to the ground (a moving platform does not make us run).
    const vec3 ground_v = grounded ? from_jolt(c.cv->GetGroundVelocity()) : vec3{};
    const vec3 horizontal{c.velocity.x - ground_v.x, 0.0f, c.velocity.z - ground_v.z};
    const f32 speed = length(horizontal);

    u32 state = 0;
    if (!grounded) {
        state = (c.jumping && c.velocity.y > kMoveSpeedThreshold) ? state_index("Jumping") : state_index("Freefall");
        c.was_airborne = true;
    } else {
        if (c.was_airborne) {
            c.landed_timer = kLandedSeconds;
            c.was_airborne = false;
        }
        c.jumping = false;
        if (c.landed_timer > 0.0f) {
            c.landed_timer -= dt;
            state = state_index("Landed");
        } else {
            state = speed > kMoveSpeedThreshold ? state_index("Running") : state_index("Idle");
        }
    }

    // Writes into the model are our own: the listener must not treat them as edits.
    w.writing_back = true;
    root->set("Position", world::Value::vector3(new_root));
    if (hum->get_bool("AutoRotate", true) && length(c.move_dir) > kMoveEpsilon) {
        // Forward of a part is -Z; yaw that turns -Z towards the movement direction.
        const f32 yaw = degrees(std::atan2(-c.move_dir.x, -c.move_dir.z));
        root->set("Orientation", world::Value::vector3({0.0f, yaw, 0.0f}));
    }
    root->set("AssemblyLinearVelocity", world::Value::vector3(c.velocity));
    hum->set("MoveDirection", world::Value::vector3(c.move_dir), true);
    hum->set("State", world::Value::enumeration(state), true);
    w.writing_back = false;
    c.last_root_pos = new_root;
}

void character_detach(CharacterImpl& c) {
    c.world = nullptr;
    c.cv = nullptr;
}

} // namespace detail

using detail::CharacterImpl;
using detail::WorldAccess;
using detail::WorldImpl;

CharacterController::CharacterController(PhysicsWorld& pw, world::Instance& model)
    : impl_(std::make_unique<CharacterImpl>()) {
    WorldImpl& w = WorldAccess::impl(pw);
    world::Instance* hum = model.find_first_child_of_class("Humanoid");
    world::Instance* root = model.find_first_child("HumanoidRootPart");
    if (!hum || !root || !root->is_a("BasePart")) {
        TEFRA_LOG_WARN("character %s needs a Humanoid and a HumanoidRootPart", model.full_name().c_str());
        return;
    }
    const vec3 size = root->get_vec3("Size", {2.0f, 2.0f, 1.0f});
    const f32 radius = clamp(min(size.x, size.z) * 0.5f, kMinRadius, kMaxRadius);
    // Capsule from the feet (origin of the CharacterVirtual) up to the top of the root part: HipHeight + Size.y.
    const f32 total = max(root_offset(*hum, *root) + size.y * 0.5f, 2.0f * radius + 0.1f);

    JPH::CapsuleShapeSettings capsule(total * 0.5f - radius, radius);
    JPH::Shape::ShapeResult shape_result = capsule.Create();
    if (shape_result.HasError()) {
        TEFRA_LOG_WARN("character: capsule failed: %s", shape_result.GetError().c_str());
        return;
    }
    JPH::CharacterVirtualSettings settings;
    settings.mShape = shape_result.Get();
    settings.mShapeOffset = JPH::Vec3(0.0f, total * 0.5f, 0.0f); // origin at the feet
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(kMaxSlopeDegrees);
    settings.mMass = kCharacterMass;
    settings.mUp = JPH::Vec3::sAxisY();

    const vec3 root_pos = root->get_vec3("Position");
    const vec3 feet = root_pos - vec3{0.0f, root_offset(*hum, *root), 0.0f};
    impl_->cv =
        new JPH::CharacterVirtual(&settings, JPH::RVec3(to_jolt(feet)), JPH::Quat::sIdentity(), model.id(), &w.system);
    impl_->world = &w;
    impl_->model_id = model.id();
    impl_->root_id = root->id();
    impl_->humanoid_id = hum->id();
    impl_->last_root_pos = root_pos;
    w.characters.push_back(impl_.get());
}

CharacterController::~CharacterController() {
    if (impl_ && impl_->world) {
        auto& list = impl_->world->characters;
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (*it == impl_.get()) {
                list.erase(it);
                break;
            }
        }
    }
}

bool CharacterController::valid() const { return impl_ && impl_->world && impl_->cv; }

void CharacterController::set_move_direction(vec3 dir) {
    if (!impl_) return;
    dir.y = 0.0f;
    const f32 len = length(dir);
    impl_->move_dir = len > 1.0f ? dir / len : dir;
}

void CharacterController::jump() {
    if (impl_) impl_->jump_requested = true;
}

vec3 CharacterController::position() const { return valid() ? from_jolt(impl_->cv->GetPosition()) : vec3{}; }

vec3 CharacterController::velocity() const { return impl_ ? impl_->velocity : vec3{}; }

bool CharacterController::on_ground() const {
    return valid() && impl_->cv->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

} // namespace tefra::physics
