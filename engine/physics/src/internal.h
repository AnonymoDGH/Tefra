#pragma once

// Private to engine/physics: the Jolt types live here, never in the public headers.

#include "tefra/physics/character.h"
#include "tefra/physics/physics_world.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tefra::physics::detail {

// Object layers: solid or sensor (CanCollide=false), anchored (static) or not (dynamic). Static-vs-static and
// sensor-vs-sensor never test; a sensor reports contacts with everything else.
inline constexpr JPH::ObjectLayer kLayerStatic = 0;
inline constexpr JPH::ObjectLayer kLayerDynamic = 1;
inline constexpr JPH::ObjectLayer kLayerStaticSensor = 2;
inline constexpr JPH::ObjectLayer kLayerDynamicSensor = 3;
inline constexpr JPH::ObjectLayer kLayerCount = 4;

inline bool is_static_layer(JPH::ObjectLayer l) { return l == kLayerStatic || l == kLayerStaticSensor; }
inline bool is_sensor_layer(JPH::ObjectLayer l) { return l == kLayerStaticSensor || l == kLayerDynamicSensor; }

// Broad phase: 0 = non-moving (static bodies), 1 = moving (dynamic bodies).
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
  public:
    unsigned int GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return JPH::BroadPhaseLayer(is_static_layer(l) ? 0 : 1);
    }
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer l1, JPH::BroadPhaseLayer l2) const override {
        if (is_static_layer(l1)) return l2.GetValue() == 1; // static never meets static
        return true;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (is_static_layer(a) && is_static_layer(b)) return false;
        if (is_sensor_layer(a) && is_sensor_layer(b)) return false;
        return true;
    }
};

struct BodyInfo {
    bool sensor = false;
    bool can_touch = true;
};

// Contact callbacks run on Jolt's worker threads: they only queue instance-id pairs; step() dispatches them.
class ContactEvents final : public JPH::ContactListener {
  public:
    JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                          const JPH::CollideShapeResult&) override {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&,
                        JPH::ContactSettings&) override;

    std::mutex mutex;
    std::vector<std::pair<u64, u64>> pending;
    const std::unordered_map<u64, BodyInfo>* infos = nullptr; // read-only while Jolt updates
};

struct TouchEntry {
    usize handle = 0;
    TouchedFn fn;
};

// Dirty flags for a part whose model state changed since the last sync.
inline constexpr u32 kDirtyRebuild = 1u << 0;  // shape, motion or collision settings: recreate the body
inline constexpr u32 kDirtyTeleport = 1u << 1; // Position / Orientation changed
inline constexpr u32 kDirtyVelocity = 1u << 2; // AssemblyLinearVelocity changed

struct CharacterImpl;

// Registers Jolt's allocator and diagnostics once per process. Returns true (used as a member initialiser).
bool ensure_jolt_initialized();

struct WorldImpl {
    explicit WorldImpl(world::DataModel& m);
    ~WorldImpl();
    WorldImpl(const WorldImpl&) = delete;
    WorldImpl& operator=(const WorldImpl&) = delete;

    f32 gravity() const;
    bool is_simulated(const world::Instance& inst) const;
    void mark(world::Instance& inst, u32 flags);
    void mark_tree(world::Instance& inst);
    void on_changed(world::Instance& inst, u32 prop);
    void sync();
    void create_body(world::Instance& part);
    void destroy_body(u64 id);
    void write_back();
    void destroy_fallen();
    void dispatch_touched();

    // First member: Jolt's temp allocator (below) allocates through the registered allocator.
    bool jolt_ready = ensure_jolt_initialized();
    world::DataModel& model;
    usize listener = 0;
    std::unordered_map<u64, BodyInfo> infos;
    std::unordered_map<u64, JPH::BodyID> bodies;
    std::unordered_map<u64, u32> dirty;
    std::vector<TouchEntry> touched;
    usize next_touch_handle = 1;
    std::vector<CharacterImpl*> characters;
    f32 accumulator = 0.0f;
    bool writing_back = false;
    bool update_error_logged = false;
    JPH::BodyID heightfield_body; // the landscape heightfield (set_heightfield); valid only when has_heightfield
    bool has_heightfield = false;
    void clear_heightfield();
    // Declaration order matters: the system (last) is destroyed first, while its filters and allocators still live.
    BroadPhaseLayers broad_layers;
    ObjectVsBroadPhase object_vs_broad;
    ObjectPairFilter pair_filter;
    ContactEvents contacts;
    JPH::TempAllocatorImpl temp;
    JPH::JobSystemThreadPool jobs;
    JPH::PhysicsSystem system;
};

struct CharacterImpl {
    WorldImpl* world = nullptr;
    u64 model_id = 0;
    u64 root_id = 0; // HumanoidRootPart
    u64 humanoid_id = 0;
    JPH::Ref<JPH::CharacterVirtual> cv;
    vec3 move_dir{};
    bool jump_requested = false;
    bool jumping = false;
    bool was_airborne = false;
    f32 landed_timer = 0.0f;
    vec3 velocity{};
    vec3 last_root_pos{};
};

// Defined in character.cpp.
void character_pre_step(CharacterImpl& c, WorldImpl& w, f32 dt);
void character_post_step(CharacterImpl& c, WorldImpl& w, f32 dt);
void character_detach(CharacterImpl& c);

// Lets CharacterController reach the world without exposing Jolt in physics_world.h.
struct WorldAccess {
    static WorldImpl& impl(PhysicsWorld& w);
};

// Conversions between Tefra and Jolt math (Y up, metres: no axis change).
inline JPH::Vec3 to_jolt(vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
inline vec3 from_jolt(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; } // RVec3 is Vec3 without double precision
inline JPH::Quat to_jolt(quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline quat from_jolt(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

} // namespace tefra::physics::detail
