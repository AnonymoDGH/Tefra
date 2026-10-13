#pragma once

// Humanoid character (spec M3, ADR-014): a Jolt CharacterVirtual (capsule) driven by a Model that has a Humanoid and
// a BasePart named "HumanoidRootPart". The controller moves the root part; the rest of the model is not simulated
// (no bodies for its parts), so animation can drive it. Humanoid.WalkSpeed, JumpHeight, HipHeight, AutoRotate and
// Humanoid.State are used and written. Walks up steps and slopes (45 degrees, steps up to 0.4 m) and pushes dynamic
// bodies.

#include "tefra/core/math.h"
#include "tefra/world/datamodel.h"

#include <memory>

namespace tefra::physics {

class PhysicsWorld;

namespace detail {
struct CharacterImpl;
} // namespace detail

class CharacterController {
  public:
    // The model must contain a Humanoid and a BasePart "HumanoidRootPart". If not, valid() is false.
    CharacterController(PhysicsWorld& world, world::Instance& model);
    ~CharacterController();
    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;

    bool valid() const;
    // Horizontal intent in world space (y is ignored). Speed = Humanoid.WalkSpeed x |dir| (|dir| is clamped to 1).
    void set_move_direction(vec3 dir);
    // Jumps when grounded: reaches Humanoid.JumpHeight. Buffered for a moment before landing and allowed shortly after
    // leaving a ledge ("coyote time"), so jumps feel responsive.
    void jump();
    // Space still held: releasing it early cuts the rise (short hops). Defaults to held.
    void set_jump_held(bool held);
    // Multiplies Humanoid.WalkSpeed (sprint). 1 = walk.
    void set_speed_scale(f32 scale);
    // Facing override (shift lock / first person): the character turns towards dir instead of its movement.
    // A zero vector restores AutoRotate towards the movement.
    void set_facing(vec3 dir);

    vec3 position() const; // feet (m); the root part centre is HipHeight + Size.y / 2 above it
    vec3 velocity() const; // m/s
    bool on_ground() const;

  private:
    std::unique_ptr<detail::CharacterImpl> impl_;
};

} // namespace tefra::physics
