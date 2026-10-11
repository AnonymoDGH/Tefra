#include "tefra/audio/sound_math.h"

#include "tefra/core/fs.h"

#include <cmath>

namespace tefra::audio {

namespace {
constexpr f32 kMinDistance = 0.01f; // avoids a division by zero for RollOffMinDistance = 0

bool is_absolute(std::string_view p) {
    return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
}
} // namespace

f32 attenuation_gain(f32 distance, f32 min_distance, f32 max_distance, f32 rolloff_scale, Falloff falloff) {
    const f32 lo = max(min_distance, kMinDistance);
    const f32 hi = max(max_distance, lo + kMinDistance);
    const f32 d = max(distance, 0.0f);
    if (d <= lo) return 1.0f;
    // The rolloff stretches the distance beyond the min distance: 2 reaches the max distance at half the range.
    const f32 de = lo + (d - lo) * max(rolloff_scale, 0.0f);
    if (de >= hi) return 0.0f;
    const f32 t = (de - lo) / (hi - lo); // 0 at the min distance, 1 at the max distance
    if (falloff == Falloff::Linear) return 1.0f - t;
    const f32 x = 1.0f - t;
    const f32 tail = x * x * (3.0f - 2.0f * x); // smoothstep: no step at the max distance
    return clamp(lo / de * tail, 0.0f, 1.0f);
}

Listener listener_from_camera(vec3 position, f32 yaw_rad, f32 pitch_rad) {
    Listener l;
    l.position = position;
    // Same convention as GameRuntime::update_camera: the camera looks along the orbit direction.
    l.forward =
        vec3{-std::sin(yaw_rad) * std::cos(pitch_rad), std::sin(pitch_rad), -std::cos(yaw_rad) * std::cos(pitch_rad)};
    return l;
}

bool is_builtin_name(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::string builtin_sound_path(std::string_view name, const std::vector<std::string>& base_dirs) {
    if (!is_builtin_name(name)) return {};
    const std::string file = std::string(name) + ".wav";
    for (const std::string& base : base_dirs) {
        if (base.empty()) continue;
        const std::string path = path_join(path_join(path_join(base, "resources"), "sfx"), file);
        if (file_exists(path.c_str())) return path;
    }
    return {};
}

std::string resolve_sound_id(std::string_view id, const std::function<std::string(std::string_view)>& project_resolve,
                             const std::vector<std::string>& base_dirs, bool* unsupported) {
    if (unsupported) *unsupported = false;
    if (id.empty()) return {};
    if (id.starts_with("rbxassetid://")) {
        if (unsupported) *unsupported = true;
        return {};
    }
    constexpr std::string_view kBuiltin = "builtin:";
    if (id.starts_with(kBuiltin)) return builtin_sound_path(id.substr(kBuiltin.size()), base_dirs);
    if (is_absolute(id)) return std::string(id);
    if (project_resolve) return project_resolve(id);
    return std::string(id);
}

} // namespace tefra::audio
