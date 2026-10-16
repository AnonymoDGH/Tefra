#pragma once

// Sky of a DataModel: the Lighting service with its Sky, Atmosphere and Clouds children become RenderSettings and
// AtmosphereParams (pure functions, no GPU, so they are unit-tested). Tefra Studio applies them every frame and
// re-bakes the image-based light only when the sky really changed (see App::update_lighting).

#include "tefra/core/math.h"
#include "tefra/renderer/atmosphere.h"
#include "tefra/renderer/renderer.h"
#include "tefra/world/datamodel.h"

#include <string>

namespace tefra::world {

enum class SkyboxMode : u8 { Procedural, Cubemap, Hdri };

// Where the visible sky and the image-based light come from (the Sky object). Equal descriptions need no re-bake.
struct SkyBackground {
    SkyboxMode mode = SkyboxMode::Procedural;
    std::string hdri;     // Radiance .hdr (HDRI)
    std::string faces[6]; // Ft, Bk, Lf, Rt, Up, Dn (Cubemap)
    bool operator==(const SkyBackground& o) const;
    bool operator!=(const SkyBackground& o) const { return !(*this == o); }
};

// Sun direction from the hour (equinox), latitude and an azimuth offset. World: +X east, +Y up, -Z north.
vec3 sun_direction(f32 clock, f32 latitude_deg, f32 azimuth_deg);
// Moon direction for a phase (0 new, 0.5 full): the sun turned by the elongation around the sun's normal axis.
vec3 moon_direction(vec3 sun, f32 phase);

SkyBackground sky_background(const DataModel& dm);
// AtmosphereParams of the Atmosphere object (Haze, Decay) and the Sky object (SunAngularSize). Defaults when absent.
void atmosphere_params(const DataModel& dm, render::AtmosphereParams& out);
bool same_atmosphere(const render::AtmosphereParams& a, const render::AtmosphereParams& b);
// Every render setting that Lighting, Sky, Atmosphere and Clouds drive: the sun (raw, may be below the horizon), the
// exposure, fog, clouds, grading, stars, moon and glare. Leaves the other settings alone. A Lighting service is
// required: without one nothing changes.
void sky_settings(const DataModel& dm, render::RenderSettings& s);

// Cloud defaults when there is no Clouds object (the values of RenderSettings).
constexpr f32 kDefaultCloudAltitude = 1600.0f;
constexpr f32 kDefaultCloudSize = 2200.0f;

} // namespace tefra::world
