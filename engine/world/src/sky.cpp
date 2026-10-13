#include "tefra/world/sky.h"

#include <cmath>

namespace tefra::world {

namespace {

constexpr f32 kFogPerDensity = 0.01f; // 1/m of height fog per unit of Atmosphere.Density
constexpr f32 kFullMoonLux = 40.0f;   // moonlight at full moon (the sun is ~30000 lux at noon)
constexpr f32 kDefaultCloudWindX = 0.9578f, kDefaultCloudWindZ = 0.2873f; // WindDirection 16.7 degrees

const Instance* child(const DataModel& dm, const char* cls) {
    const Instance* light = dm.service("Lighting");
    return light ? light->find_first_child_of_class(cls) : nullptr;
}

u32 enum_of(const Instance& inst, const char* prop, u32 def) {
    const Value* v = inst.get(prop);
    return v && v->type == PropType::Enum ? v->as_enum() : def;
}

} // namespace

bool SkyBackground::operator==(const SkyBackground& o) const {
    if (mode != o.mode || hdri != o.hdri) return false;
    for (int i = 0; i < 6; ++i)
        if (faces[i] != o.faces[i]) return false;
    return true;
}

vec3 sun_direction(f32 clock, f32 latitude_deg, f32 azimuth_deg) {
    const f32 h = (clock - 12.0f) / 24.0f * kTwoPi;
    const f32 phi = radians(latitude_deg);
    const f32 east = -std::sin(h);
    const f32 up = std::cos(phi) * std::cos(h);
    const f32 north = -std::sin(phi) * std::cos(h);
    vec3 d{east, up, -north};
    const f32 a = radians(azimuth_deg);
    d = {d.x * std::cos(a) - d.z * std::sin(a), d.y, d.x * std::sin(a) + d.z * std::cos(a)};
    return normalize(d);
}

vec3 moon_direction(vec3 sun, f32 phase) {
    // Rotation of the sun vector by the elongation theta = phase * 2 pi about k (k is perpendicular to the sun).
    const f32 theta = phase * kTwoPi;
    const vec3 k = std::fabs(sun.y) > 0.99f ? vec3{1.0f, 0.0f, 0.0f} : normalize(cross(sun, vec3{0.0f, 1.0f, 0.0f}));
    return normalize(sun * std::cos(theta) + cross(k, sun) * std::sin(theta));
}

SkyBackground sky_background(const DataModel& dm) {
    SkyBackground bg;
    const Instance* sky = child(dm, "Sky");
    if (!sky) return bg;
    const u32 mode = enum_of(*sky, "SkyboxMode", 0);
    if (mode == 2) {
        bg.mode = SkyboxMode::Hdri;
        bg.hdri = sky->get_string("HdriPath");
    } else if (mode == 1) {
        bg.mode = SkyboxMode::Cubemap;
        static constexpr const char* kProps[6] = {"SkyboxFt", "SkyboxBk", "SkyboxLf",
                                                  "SkyboxRt", "SkyboxUp", "SkyboxDn"};
        for (int i = 0; i < 6; ++i) bg.faces[i] = sky->get_string(kProps[i]);
    }
    return bg;
}

void atmosphere_params(const DataModel& dm, render::AtmosphereParams& p) {
    p = render::AtmosphereParams{};
    if (const Instance* a = child(dm, "Atmosphere")) {
        p.haze = clamp(a->get_float("Haze", 6.0f), 0.0f, 50.0f);
        const vec3 decay = a->get_vec3("Decay", vec3{1.0f, 1.0f, 1.0f});
        const vec3 r = p.rayleigh_scattering;
        p.rayleigh_scattering =
            vec3{r.x * clamp(decay.x, 0.0f, 2.0f), r.y * clamp(decay.y, 0.0f, 2.0f), r.z * clamp(decay.z, 0.0f, 2.0f)};
    }
    if (const Instance* s = child(dm, "Sky"))
        p.sun_angular_radius = radians(clamp(s->get_float("SunAngularSize", 0.5358f), 0.05f, 10.0f) * 0.5f);
}

bool same_atmosphere(const render::AtmosphereParams& a, const render::AtmosphereParams& b) {
    return a.haze == b.haze && a.sun_angular_radius == b.sun_angular_radius &&
           a.rayleigh_scattering.x == b.rayleigh_scattering.x && a.rayleigh_scattering.y == b.rayleigh_scattering.y &&
           a.rayleigh_scattering.z == b.rayleigh_scattering.z;
}

void sky_settings(const DataModel& dm, render::RenderSettings& s) {
    const Instance* light = dm.service("Lighting");
    if (!light) return;
    const f32 clock = light->get_float("ClockTime", 14.0f);
    const f32 lat = light->get_float("GeographicLatitude", 20.0f);
    const f32 az = light->get_float("SunAzimuthOffset", 0.0f);
    // The raw sun: below the horizon at night. The renderer takes the moon as the light then (see Renderer::render).
    const vec3 sun = sun_direction(clock, lat, az);
    s.sun_direction = sun;
    // Sun strength for the lights without an atmosphere (with one, the renderer takes it from the atmosphere).
    s.sun_illuminance = 30000.0f * saturate((sun.y + 0.1f) / 0.15f);
    // Exposure follows the sun height (bright noon, dark night), plus the user's compensation.
    const f32 day = saturate((sun.y + 0.05f) / 0.45f);
    s.ev100 = lerp(8.5f, 14.8f, day) - light->get_float("ExposureCompensation", 0.0f);
    s.sun_scale = max(light->get_float("SunBrightness", 1.0f), 0.0f);
    s.ibl_intensity = max(light->get_float("EnvironmentDiffuseScale", 1.0f), 0.0f);
    {
        const vec3 c = light->get_vec3("IndoorAmbient", vec3{0, 0, 0});
        s.ambient = c * (max(light->get_float("IndoorAmbientBrightness", 0.0f), 0.0f) * 100.0f);
    }
    s.grade_saturation = light->get_float("Saturation", 1.05f);
    s.grade_contrast = light->get_float("Contrast", 1.05f);
    s.fog_density = light->get_float("FogDensity", 0.0f);
    s.fog_falloff = light->get_float("FogHeightFalloff", 0.03f);
    s.cloud_coverage = light->get_float("CloudCover", 0.3f);
    s.fog_height = 0.0f;
    s.fog_tint = vec3{1.0f, 1.0f, 1.0f};
    s.sky_glare = 0.0f;

    if (const Instance* a = child(dm, "Atmosphere")) {
        s.fog_density += clamp(a->get_float("Density", 0.0f), 0.0f, 100.0f) * kFogPerDensity;
        s.fog_height = a->get_float("Offset", 0.0f);
        s.fog_tint = a->get_vec3("Color", vec3{1.0f, 1.0f, 1.0f});
        s.sky_glare = clamp(a->get_float("Glare", 0.0f), 0.0f, 10.0f);
    }

    if (const Instance* c = child(dm, "Clouds")) {
        s.cloud_coverage = c->get_bool("Enabled", true) ? clamp(c->get_float("Cover", 0.3f), 0.0f, 1.0f) : 0.0f;
        s.cloud_density = clamp(c->get_float("Density", 1.0f), 0.0f, 10.0f);
        s.cloud_tint = c->get_vec3("Color", vec3{1.0f, 1.0f, 1.0f});
        s.cloud_altitude = max(c->get_float("Altitude", kDefaultCloudAltitude), 100.0f);
        s.cloud_size = max(c->get_float("Size", kDefaultCloudSize), 100.0f);
        s.cloud_wind = max(c->get_float("WindSpeed", 6.0f), 0.0f);
        const f32 wd = radians(c->get_float("WindDirection", 16.7f));
        s.cloud_wind_dir = vec2{std::cos(wd), std::sin(wd)};
    } else {
        s.cloud_density = 1.0f;
        s.cloud_tint = vec3{1.0f, 1.0f, 1.0f};
        s.cloud_altitude = kDefaultCloudAltitude;
        s.cloud_size = kDefaultCloudSize;
        s.cloud_wind = 6.0f;
        s.cloud_wind_dir = vec2{kDefaultCloudWindX, kDefaultCloudWindZ};
    }

    // Sky: stars, sun and moon discs, moon phase and light.
    const Instance* sky = child(dm, "Sky");
    s.star_count = sky ? max(sky->get_float("StarCount", 3000.0f), 0.0f) : 3000.0f;
    s.star_brightness = sky ? max(sky->get_float("StarBrightness", 1.0f), 0.0f) : 1.0f;
    s.celestial_shown = sky ? sky->get_bool("CelestialBodiesShown", true) : true;
    s.moon_visible = sky ? sky->get_bool("MoonVisible", true) : true;
    const f32 phase = sky ? clamp(sky->get_float("MoonPhase", 0.5f), 0.0f, 1.0f) : 0.5f;
    const f32 moon_size = sky ? clamp(sky->get_float("MoonAngularSize", 3.0f), 0.1f, 10.0f) : 3.0f;
    const f32 brightness = sky ? max(sky->get_float("MoonBrightness", 1.0f), 0.0f) : 1.0f;
    s.moon_phase = phase;
    s.moon_angular_radius = radians(moon_size * 0.5f);
    s.moon_direction = moon_direction(sun, phase);
    // Moonlight follows the lit fraction: 0 at new moon, full at 0.5.
    const f32 lit = 0.5f - 0.5f * std::cos(kTwoPi * phase);
    s.moon_illuminance = s.moon_visible ? kFullMoonLux * brightness * lit : 0.0f;
}

} // namespace tefra::world
