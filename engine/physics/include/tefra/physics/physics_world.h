#pragma once

// Physics for the DataModel (spec M3, ADR-014): every BasePart of the Workspace becomes a Jolt body. Anchored parts
// are static, the rest dynamic (mass from Density x volume). CanCollide=false makes a sensor: no collisions, but
// Touched still fires. Simulation runs at a fixed 60 Hz; dynamic bodies write Position, Orientation and
// AssemblyLinearVelocity back to the model. Scripts and the editor change the model through WorldListener and the
// bodies follow (teleport, rebuild, velocity). Jolt types stay in engine/physics/src.
//
// Play mode: on_play_start creates a PhysicsWorld, on_play_step calls step(dt), on_play_stop destroys it. Destroy
// every CharacterController before its PhysicsWorld (a leftover controller is detached, not crashed).

#include "tefra/core/math.h"
#include "tefra/world/datamodel.h"

#include <functional>
#include <memory>

namespace tefra::physics {

namespace detail {
struct WorldImpl;
struct WorldAccess;
} // namespace detail

inline constexpr f32 kPhysicsStep = 1.0f / 60.0f; // fixed substep (s)

struct RaycastResult {
    world::Instance* instance = nullptr; // nullptr when nothing was hit
    vec3 position{};                     // hit point (m)
    vec3 normal{};                       // surface normal at the hit (unit)
    f32 distance = 0.0f;                 // from the origin (m)
};

// Touched(partA, partB): fired once per new contact pair (order is not guaranteed). Called from step() on the calling
// thread, never from Jolt's worker threads. Listeners may change the model.
using TouchedFn = std::function<void(world::Instance& a, world::Instance& b)>;

class PhysicsWorld {
  public:
    explicit PhysicsWorld(world::DataModel& model);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Advances the simulation by dt seconds in fixed substeps of kPhysicsStep (at most 8 per call; the frame time is
    // clamped to 0.25 s). Applies pending model changes first and writes the results back afterwards.
    void step(f32 dt);
    // Applies pending model changes without simulating (call after edits, before raycast).
    void sync();

    // Closest solid (non-sensor) part along the ray. direction need not be normalised; max_distance in metres.
    bool raycast(vec3 origin, vec3 direction, f32 max_distance, RaycastResult& out) const;

    // Static landscape heightfield (one per world; a new call replaces the old one): n x n samples, row-major, the
    // sample (x, z) sits at origin_xz + (x, z) * cell with height heights[z * n + x]. n must be even (Jolt's block
    // size is 2). owner is the Terrain instance that raycasts report. The heights are copied.
    void set_heightfield(const f32* heights, u32 n, f32 cell, vec2 origin_xz, const world::Instance* owner);
    void clear_heightfield();
    bool has_heightfield() const;

    usize add_touched_listener(TouchedFn fn);
    void remove_touched_listener(usize handle);

    bool has_body(const world::Instance& part) const;
    u32 body_count() const;
    world::DataModel& model() const { return model_; }

  private:
    friend struct detail::WorldAccess;
    world::DataModel& model_;
    std::unique_ptr<detail::WorldImpl> impl_;
};

} // namespace tefra::physics
