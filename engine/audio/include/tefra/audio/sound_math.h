#pragma once

// Pure helpers of the audio system: no device, no DataModel. Unit-tested in tests/unit/test_audio.cpp.

#include "tefra/core/math.h"
#include "tefra/core/types.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::audio {

enum class Falloff : u8 {
    Inverse, // gain = min / d with a smooth tail that reaches 0 at the max distance (default)
    Linear,  // gain falls in a straight line from 1 at the min distance to 0 at the max distance
};

// Gain in [0, 1] of a 3D sound at `distance` metres: 1 up to min_distance, 0 from max_distance on. rolloff_scale
// stretches the falloff curve (SoundService.RolloffScale): 2 falls off twice as fast, 0.5 half as fast.
f32 attenuation_gain(f32 distance, f32 min_distance, f32 max_distance, f32 rolloff_scale = 1.0f,
                     Falloff falloff = Falloff::Inverse);

// Position and forward direction of the listener (the camera). Forward is a unit vector; up is world +Y.
struct Listener {
    vec3 position{};
    vec3 forward{0.0f, 0.0f, -1.0f};
};
Listener listener_from_camera(vec3 position, f32 yaw_rad, f32 pitch_rad); // yaw 0 looks down -Z

// True for names usable in "builtin:<name>": non-empty, [a-z0-9_] only (no separators, no dots).
bool is_builtin_name(std::string_view name);

// The file of a built-in sound: <base>/resources/sfx/<name>.wav for the first base folder where it exists, or ""
// when the name is invalid or no file exists. The order of `base_dirs` is the search order.
std::string builtin_sound_path(std::string_view name, const std::vector<std::string>& base_dirs);

// Resolves a SoundId to a file: "" for an empty id; "builtin:<name>" through builtin_sound_path; absolute paths as
// they are; relative paths through `project_resolve` (GameRuntime::resolve_asset). rbxassetid:// ids cannot be
// resolved: *unsupported is set and "" is returned.
std::string resolve_sound_id(std::string_view id, const std::function<std::string(std::string_view)>& project_resolve,
                             const std::vector<std::string>& base_dirs, bool* unsupported);

} // namespace tefra::audio
