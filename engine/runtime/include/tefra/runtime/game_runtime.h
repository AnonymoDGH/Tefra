#pragma once

// Game runtime shared by Tefra Studio (Play) and the standalone player (spec M3b §2). Owns one play session over a
// DataModel: Players/LocalPlayer + PlayerGui, the R15 character with its controller, Jolt physics, the Animator,
// C# scripts, humanoid health (death and respawn), the third-person camera, and pluggable RuntimeSystems (audio,
// in-game UI, ...). The host feeds an InputState every frame and renders the model with SceneSync.
//
// Step order: input -> scripts -> systems.pre_physics -> character intent -> physics -> animator -> humanoids
// (death / respawn) -> camera -> systems.post_physics.

#include "tefra/core/input.h"
#include "tefra/core/math.h"
#include "tefra/core/types.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::world {
class DataModel;
class Instance;
} // namespace tefra::world
namespace tefra::physics {
class PhysicsWorld;
class CharacterController;
} // namespace tefra::physics
namespace tefra::anim {
class AnimatorSystem;
}
namespace tefra::scripting {
class ScriptHost;
}

namespace tefra::runtime {

class GameRuntime;

// Extension point for engine modules that live only while the game runs. No RTTI: look systems up by name().
class RuntimeSystem {
  public:
    virtual ~RuntimeSystem() = default;
    virtual const char* name() const = 0;
    // After the character spawned and the scripts started. Returning false only logs a warning.
    virtual bool start(GameRuntime& rt) {
        (void)rt;
        return true;
    }
    virtual void pre_physics(GameRuntime& rt, f32 dt) {
        (void)rt;
        (void)dt;
    }
    // After physics, animation and the camera of this frame.
    virtual void post_physics(GameRuntime& rt, f32 dt) {
        (void)rt;
        (void)dt;
    }
    virtual void stop(GameRuntime& rt) { (void)rt; }
};

struct RuntimeConfig {
    scripting::ScriptHost* scripts = nullptr; // optional; the caller keeps .NET alive between sessions
    bool spawn_character = true;
    std::string player_name = "Player";
    std::string project_dir; // base for relative asset paths (sounds, images, meshes); empty = working directory
};

struct CameraState {
    vec3 position{0, 5, 10};
    f32 yaw = 0.0f, pitch = 0.0f; // radians; yaw 0 looks down -Z
    f32 fov_y = radians(70.0f);
};

class GameRuntime {
  public:
    GameRuntime(world::DataModel& model, RuntimeConfig cfg);
    ~GameRuntime(); // stops a running session
    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    // Systems are started in the order they were added and stopped in reverse. Add before start().
    void add_system(std::unique_ptr<RuntimeSystem> system);
    RuntimeSystem* system(std::string_view name) const;

    bool start();
    void step(f32 dt, const InputState& input);
    void stop();
    bool running() const { return running_; }

    world::DataModel& model() const { return model_; }
    physics::PhysicsWorld* physics() const { return physics_.get(); }
    physics::CharacterController* controller() const { return controller_.get(); }
    anim::AnimatorSystem* animator() const { return animator_.get(); }
    scripting::ScriptHost* scripts() const { return cfg_.scripts; }
    world::Instance* local_player() const; // Players.<player_name>
    world::Instance* player_gui() const;   // LocalPlayer.PlayerGui (StarterGui is cloned into it at spawn)
    world::Instance* character() const;    // the avatar Model, nullptr while dead / without a character
    world::Instance* humanoid() const;
    const InputState& input() const { return input_; }
    const CameraState& camera() const { return camera_; }
    // The host should hide and lock the cursor (relative mouse) while this is true: orbiting with the right button,
    // shift lock (Left Ctrl toggles it) or first person (zoomed all the way in).
    bool mouse_locked() const { return mouse_locked_; }
    bool shift_lock() const { return shift_lock_; }
    bool first_person() const { return cam_dist_ < kFirstPersonDist; }
    void set_camera(const CameraState& c) { camera_ = c; } // e.g. a script or the host overriding the view
    const std::string& project_dir() const { return cfg_.project_dir; }
    std::string resolve_asset(std::string_view path) const; // relative -> project_dir/path
    f64 time() const { return time_; }
    u64 frame() const { return frame_; }

    // Kills / respawns the local character now (scripts use Humanoid.Health = 0 instead).
    void respawn();

  private:
    void spawn_character();
    void despawn_character();
    void update_humanoids(f32 dt);
    void update_camera(f32 dt);

    world::DataModel& model_;
    RuntimeConfig cfg_;
    bool running_ = false;
    std::vector<std::unique_ptr<RuntimeSystem>> systems_;
    std::unique_ptr<physics::PhysicsWorld> physics_;
    std::unique_ptr<physics::CharacterController> controller_;
    std::unique_ptr<anim::AnimatorSystem> animator_;
    usize touched_listener_ = ~usize(0);
    u64 player_id_ = 0, character_id_ = 0;
    f64 died_at_ = -1.0; // time of death of the local character, -1 = alive
    f64 time_ = 0.0;
    u64 frame_ = 0;
    InputState input_;
    CameraState camera_;
    static constexpr f32 kFirstPersonDist = 0.6f;
    f32 orbit_yaw_ = 0.0f, orbit_pitch_ = radians(-15.0f), orbit_dist_ = 9.0f;
    f32 cam_dist_ = 9.0f;     // smoothed zoom (orbit_dist_ is the target)
    f32 cam_hit_dist_ = 1e9f; // collision-limited distance, eased back out
    f32 cam_target_y_ = 0.0f; // smoothed follow height (softens stair steps)
    f32 cam_fov_ = radians(70.0f);
    bool cam_init_ = false;
    bool shift_lock_ = false, mouse_locked_ = false, sprinting_ = false;
    f32 last_health_ = 0.0f; // last seen health of the local humanoid (HealthChanged)
};

} // namespace tefra::runtime
