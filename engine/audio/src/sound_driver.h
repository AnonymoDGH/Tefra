#pragma once

// Internal: maps the Sound instances of a DataModel onto backend voices, once per frame. No runtime dependency:
// the AudioSystem passes the listener and the path resolver in, so the logic can be tested with the null backend.

#include "backend.h"

#include "tefra/audio/sound_math.h"
#include "tefra/core/math.h"
#include "tefra/core/types.h"
#include "tefra/world/datamodel.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace tefra::audio {

struct SoundPaths {
    std::function<std::string(std::string_view)> project_resolve; // relative SoundId -> file (GameRuntime)
    std::vector<std::string> base_dirs;                           // folders that hold resources/sfx
};

class SoundDriver {
  public:
    SoundDriver(Backend& backend, SoundPaths paths);
    ~SoundDriver(); // drops every voice while the backend still exists
    SoundDriver(const SoundDriver&) = delete;
    SoundDriver& operator=(const SoundDriver&) = delete;

    // Once per frame, after the step. Reads SoundService (Volume, RolloffScale) and every Sound under the root.
    void update(world::DataModel& dm, const Listener& listener, f32 dt);

    // One-shot voice that plays to its end and is then dropped. position = nullptr plays 2D. False when the budget
    // of one-shots is full, the path is empty or the file cannot be decoded.
    bool play_one_shot(const std::string& path, f32 volume, f32 pitch, const vec3* position);

    // Drops every voice and clip. Sound instances keep their properties (TimePosition is the caller's to reset).
    void stop_all();

    u32 active_voices() const;
    u32 loaded_clips() const { return static_cast<u32>(clips_.size()); }

  private:
    struct Tracked {
        bool loaded = false;
        std::string sound_id; // the SoundId the voice was loaded from
        bool spatial = false; // the parent is a part: 3D voice
        std::shared_ptr<const ClipData> clip;
        std::unique_ptr<Voice> voice;
        f64 pos = 0.0; // last position written to TimePosition
        // Snapshot taken every frame: the Sound is gone when the driver next looks, so PlayOnRemove uses this.
        bool play_on_remove = false;
        f32 volume = 0.5f;
        f32 pitch = 1.0f;
        f32 min_distance = 3.0f;
        f32 max_distance = 120.0f;
        vec3 where{};
    };
    struct OneShot {
        std::unique_ptr<Voice> voice;
        std::shared_ptr<const ClipData> clip; // keeps the samples alive while the voice plays
    };

    void sync_sound(world::DataModel& dm, world::Instance& s, Tracked& t, f32 dt);
    void load(world::Instance& s, Tracked& t, bool spatial);
    void fire_removed(Tracked& t);
    bool start_one_shot(std::shared_ptr<const ClipData> clip, f32 gain, f32 pitch, const vec3* where);
    f32 distance_gain(vec3 where, f32 min_distance, f32 max_distance) const;
    bool anchor_position(const world::DataModel& dm, const world::Instance& s, vec3& out) const;
    void warn_once(const std::string& key, const std::string& message);

    Backend& backend_;
    SoundPaths paths_;
    ClipCache clips_;
    Listener listener_;
    f32 master_ = 1.0f;
    f32 rolloff_ = 1.0f;
    std::unordered_map<u64, Tracked> tracked_;
    std::vector<OneShot> oneshots_;
    std::set<std::string> warned_;
};

} // namespace tefra::audio
