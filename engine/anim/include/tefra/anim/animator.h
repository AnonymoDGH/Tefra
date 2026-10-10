#pragma once

// AnimatorSystem (spec M3 ADR-016): drives every Humanoid that has an Animator inside a Model.
//
// Each update it
//   1. picks the locomotion clips from the root part's horizontal speed: idle, walk and run blend by speed
//      (walk and run share one gait phase, so their feet stay in step);
//   2. picks the airborne clips from Humanoid.State: jump (Jumping), fall (Freefall), climb (Climbing);
//   3. cross-fades every clip over its fade time and composes the layers by priority: a higher priority clip
//      replaces the joints it animates, in proportion to its weight;
//   4. writes Position and Orientation of every Part of the rig, walking the Motor6D tree from the
//      HumanoidRootPart: Part1 = Part0 * C0 * Transform(anim) * inverse(C1).
// The root part itself is not moved: the character controller (or a test) owns it.
//
// Physics is not involved. Units: metres, seconds, degrees in Orientation.

#include "tefra/anim/clip.h"
#include "tefra/world/datamodel.h"

#include <memory>
#include <string_view>
#include <unordered_map>

namespace tefra::anim {

class AnimatorSystem {
  public:
    explicit AnimatorSystem(world::DataModel& model);
    ~AnimatorSystem();
    AnimatorSystem(const AnimatorSystem&) = delete;
    AnimatorSystem& operator=(const AnimatorSystem&) = delete;

    // Advances every rigged Humanoid by dt seconds and writes its Parts. Call once per frame.
    void update(f32 dt);

    // Plays a clip on a Humanoid. Other manual clips on the same priority fade out over `fade` seconds.
    // `weight` is the target strength (0-1); `speed` scales the playback rate. False if the instance is not a
    // Humanoid inside a Model.
    bool play(world::Instance& humanoid, ClipPtr clip, f32 fade = 0.2f, f32 weight = 1.0f, f32 speed = 1.0f);
    // Same, with a built-in clip name ("idle", "walk", "run", "jump", "fall", "climb", "wave").
    bool play(world::Instance& humanoid, std::string_view builtin, f32 fade = 0.2f, f32 weight = 1.0f,
              f32 speed = 1.0f);
    // Fades out a manual clip by name, or every manual clip when the name is empty. Locomotion is not affected.
    void stop(world::Instance& humanoid, std::string_view clip = {}, f32 fade = 0.2f);
    // True while the clip (manual or automatic) is being played with a non-zero target weight.
    bool is_playing(const world::Instance& humanoid, std::string_view clip) const;

  private:
    struct State;
    State& state_for(const world::Instance& humanoid);
    void advance(const world::Instance& humanoid, world::Instance& model, State& st, f32 dt);

    world::DataModel& model_;
    std::unordered_map<u64, std::unique_ptr<State>> states_;
};

} // namespace tefra::anim
