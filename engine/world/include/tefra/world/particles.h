#pragma once

// ParticleEmitter simulation (CPU): spawns, moves and ages the particles of every enabled emitter in the Workspace
// and builds the camera-sorted list the renderer draws (render::ParticleGPU). Runs in edit mode too, so effects are
// visible while building, and in play mode with the game clock.

#include "tefra/core/math.h"
#include "tefra/renderer/gpu_types.h"

#include <unordered_map>
#include <vector>

namespace tefra::world {

class DataModel;
class Instance;

class ParticleSystem {
  public:
    // Steps every emitter by dt seconds and rebuilds gpu() sorted back to front from camera_pos. max_particles caps
    // the total (quality presets); emitters beyond it stop spawning until particles die.
    void update(DataModel& dm, f32 dt, vec3 camera_pos, u32 max_particles = 20000);
    const std::vector<render::ParticleGPU>& gpu() const { return out_; }
    u32 count() const { return static_cast<u32>(out_.size()); }
    void clear();

  private:
    struct Particle {
        vec3 pos, vel;
        f32 age = 0.0f, life = 1.0f, size_scale = 1.0f, rot = 0.0f, rot_speed = 0.0f;
    };
    struct Emitter {
        std::vector<Particle> ps;
        f32 accum = 0.0f;
        vec3 last_origin{};
        bool has_origin = false;
        u64 seen = 0;
    };
    f32 random01();
    void spawn(const Instance& e, const Instance& part, Emitter& em, u32 count);

    DataModel* model_ = nullptr;
    u64 scanned_revision_ = ~0ull;
    std::vector<u64> emitter_ids_;
    std::unordered_map<u64, Emitter> emitters_;
    std::vector<render::ParticleGPU> out_;
    u32 rng_ = 0x9E3779B9u;
    u64 frame_ = 0;
};

} // namespace tefra::world
