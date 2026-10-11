#include "tefra/audio/audio_system.h"

#include "sound_driver.h"

#include "tefra/audio/sound_math.h"
#include "tefra/core/fs.h"
#include "tefra/core/log.h"
#include "tefra/physics/physics_world.h"
#include "tefra/world/datamodel.h"

#include <cmath>

#ifndef TEFRA_SOURCE_DIR
#define TEFRA_SOURCE_DIR ""
#endif

namespace tefra::audio {

using world::ClassRegistry;
using world::Instance;
using world::Value;

namespace {

constexpr u32 kNoState = ~0u;
constexpr f32 kStride = 2.2f;       // metres per footstep at any speed: the cadence follows the speed
constexpr f32 kMinStepSpeed = 0.5f; // m/s; slower than this makes no sound
constexpr f32 kProbe = 6.0f;        // how far below the root the ground is looked for

// Index of a HumanoidState item by name (world/src/classes.cpp); kNoState when the registry lacks it.
u32 state_index(const char* name) {
    const world::EnumInfo* e = ClassRegistry::get().find_enum("HumanoidState");
    const i32 i = e ? e->index_of(name) : -1;
    return i < 0 ? kNoState : static_cast<u32>(i);
}

// Surfaces that sound like grass (soft footsteps); everything else sounds like stone.
bool surface_is_grass(const Instance& part) {
    if (part.is_a("Terrain")) return true;
    const Value* m = part.get("Material");
    const world::EnumInfo* e = ClassRegistry::get().find_enum("Material");
    if (!m || !e) return false;
    const auto idx = static_cast<usize>(m->n[0]);
    if (idx >= e->items.size()) return false;
    const std::string& name = e->items[idx];
    return name == "Grass" || name == "Sand" || name == "Ground";
}

} // namespace

// Sounds of the local character: jump, landing, footsteps (paced by the horizontal speed) and death. They come from
// the Humanoid.State changes made by the runtime and the physics, and are 2D (the listener is the character).
class CharacterSounds {
  public:
    explicit CharacterSounds(const std::vector<std::string>& base_dirs)
        : jump_(builtin_sound_path("jump", base_dirs)), land_(builtin_sound_path("land", base_dirs)),
          death_(builtin_sound_path("death", base_dirs)), grass_(builtin_sound_path("footstep_grass", base_dirs)),
          stone_(builtin_sound_path("footstep_stone", base_dirs)), state_jumping_(state_index("Jumping")),
          state_freefall_(state_index("Freefall")), state_dead_(state_index("Dead")),
          state_walking_(state_index("Walking")), state_running_(state_index("Running")) {
        if (jump_.empty() || land_.empty() || death_.empty() || grass_.empty() || stone_.empty())
            TEFRA_LOG_WARN("audio: the built-in sound effects are missing (expected resources/sfx next to the "
                           "executable); character sounds are silent");
    }

    void reset_tracking() {
        humanoid_id_ = 0;
        have_prev_ = false;
        distance_ = 0.0f;
    }

    void update(runtime::GameRuntime& rt, SoundDriver& driver, f32 dt) {
        const Instance* service = rt.model().service("SoundService");
        Instance* hum = rt.humanoid();
        Instance* character = rt.character();
        const bool enabled = !service || service->get_bool("CharacterSounds", true);
        if (!enabled || !hum || !character) {
            reset_tracking();
            return;
        }
        if (hum->id() != humanoid_id_) {
            // A new character (first spawn or respawn): no transitions from the previous one.
            reset_tracking();
            humanoid_id_ = hum->id();
        }

        const Value* sv = hum->get("State");
        const u32 state = sv ? static_cast<u32>(sv->n[0]) : kNoState;
        if (have_prev_) {
            const bool was_air = prev_ == state_jumping_ || prev_ == state_freefall_;
            const bool air = state == state_jumping_ || state == state_freefall_;
            if (state == state_jumping_ && prev_ != state_jumping_) play(driver, jump_, 0.5f);
            if (was_air && !air && state != state_dead_) play(driver, land_, 0.55f);
            if (state == state_dead_ && prev_ != state_dead_) play(driver, death_, 0.8f);
        }
        prev_ = state;
        have_prev_ = true;

        const bool moving = state == state_walking_ || state == state_running_;
        if (!moving) {
            // The first step after standing still comes after half a stride.
            distance_ = kStride * 0.5f;
            return;
        }
        const Instance* root = character->find_first_child("HumanoidRootPart");
        if (!root) return;
        const vec3 v = root->get_vec3("AssemblyLinearVelocity");
        const f32 speed = length(vec3{v.x, 0.0f, v.z});
        if (speed < kMinStepSpeed) return;
        distance_ += speed * dt;
        if (distance_ >= kStride) {
            distance_ -= kStride;
            const bool grass = on_grass(rt, *character);
            // Small pitch variation so steps do not sound identical; deterministic (golden-ratio sequence).
            const f32 pitch = 0.95f + 0.1f * std::fmod(static_cast<f32>(steps_) * 0.618034f, 1.0f);
            ++steps_;
            driver.play_one_shot(grass ? grass_ : stone_, 0.45f, pitch, nullptr);
        }
    }

  private:
    static void play(SoundDriver& driver, const std::string& path, f32 volume) {
        driver.play_one_shot(path, volume, 1.0f, nullptr);
    }

    static bool on_grass(const runtime::GameRuntime& rt, const Instance& character) {
        const Instance* root = character.find_first_child("HumanoidRootPart");
        const physics::PhysicsWorld* pw = rt.physics();
        if (!root || !pw) return true;
        vec3 origin = root->get_vec3("Position");
        f32 remaining = kProbe;
        for (int i = 0; i < 4 && remaining > 0.0f; ++i) {
            physics::RaycastResult hit;
            if (!pw->raycast(origin, vec3{0.0f, -1.0f, 0.0f}, remaining, hit) || !hit.instance) break;
            if (!hit.instance->is_descendant_of(&character)) return surface_is_grass(*hit.instance);
            // Our own parts are skipped, as the camera does.
            const f32 advance_by = hit.distance + 0.05f;
            remaining -= advance_by;
            origin = origin + vec3{0.0f, -advance_by, 0.0f};
        }
        return true; // nothing solid below: the terrain
    }

    std::string jump_, land_, death_, grass_, stone_;
    u32 state_jumping_, state_freefall_, state_dead_, state_walking_, state_running_;
    u64 humanoid_id_ = 0;
    bool have_prev_ = false;
    u32 prev_ = kNoState;
    f32 distance_ = 0.0f;
    u32 steps_ = 0;
};

AudioSystem::AudioSystem(AudioOptions options) : options_(options) {}

AudioSystem::~AudioSystem() {
    character_.reset();
    driver_.reset();
    backend_.reset();
}

bool AudioSystem::start(runtime::GameRuntime& rt) {
    backend_ = open_backend(options_.use_device);
    runtime::GameRuntime* host = &rt;
    SoundPaths paths;
    paths.project_resolve = [host](std::string_view p) { return host->resolve_asset(p); };
    paths.base_dirs = {exe_dir(), TEFRA_SOURCE_DIR};
    driver_ = std::make_unique<SoundDriver>(*backend_, paths);
    character_ = std::make_unique<CharacterSounds>(paths.base_dirs);
    return true;
}

void AudioSystem::post_physics(runtime::GameRuntime& rt, f32 dt) {
    if (!driver_) return;
    const runtime::CameraState& cam = rt.camera();
    driver_->update(rt.model(), listener_from_camera(cam.position, cam.yaw, cam.pitch), dt);
    character_->update(rt, *driver_, dt);
}

void AudioSystem::stop(runtime::GameRuntime& rt) {
    if (!driver_) return;
    driver_->stop_all();
    // The next session starts every sound at 0; TimePosition is not saved, so it must not carry over.
    for (Instance* s : rt.model().root().descendants())
        if (s->is_a("Sound")) s->set("TimePosition", Value::number(0.0), true);
    character_.reset();
    driver_.reset();
    backend_.reset();
}

const char* AudioSystem::backend_name() const { return backend_ ? backend_->name() : "none"; }

u32 AudioSystem::active_voices() const { return driver_ ? driver_->active_voices() : 0u; }

u32 AudioSystem::loaded_clips() const { return driver_ ? driver_->loaded_clips() : 0u; }

std::unique_ptr<runtime::RuntimeSystem> make_audio_system(AudioOptions options) {
    return std::make_unique<AudioSystem>(options);
}

} // namespace tefra::audio
