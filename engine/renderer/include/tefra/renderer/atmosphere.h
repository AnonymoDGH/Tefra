#pragma once

// Physically based atmosphere (Hillaire 2020). Static LUTs (transmittance, multiple scattering) are computed on
// the CPU; the per-frame sky-view LUT is rendered on the GPU from the same model (shaders/common/atmosphere.glsl).
// Radiance values are per unit of top-of-atmosphere sun illuminance; multiply by sun_illuminance_top (lux).

#include "tefra/core/image.h"
#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

struct AtmosphereParams {
    f32 bottom_radius = 6360e3f; // m
    f32 top_radius = 6460e3f;
    vec3 rayleigh_scattering{5.802e-6f, 13.558e-6f, 33.1e-6f}; // 1/m at sea level
    f32 rayleigh_scale_height = 8000.0f;
    f32 mie_scattering = 3.996e-6f;
    f32 mie_extinction = 4.440e-6f;
    f32 mie_scale_height = 1200.0f;
    f32 mie_g = 0.8f;
    f32 haze = 6.0f; // Mie density multiplier (hazy valley)
    vec3 ozone_absorption{0.650e-6f, 1.881e-6f, 0.085e-6f};
    f32 ozone_center = 25000.0f;
    f32 ozone_half_width = 15000.0f;
    vec3 ground_albedo{0.12f, 0.11f, 0.09f};
    f32 sun_illuminance_top = 120000.0f; // lux
    f32 sun_angular_radius = 0.004675f;  // rad
    f32 ground_altitude = 300.0f;        // world y = 0 is this many meters above sea level
};

// GPU mirror (std430), shaders/common/atmosphere.glsl.
struct AtmosphereGPU {
    vec4 rayleigh; // xyz scattering, w scale height
    vec4 mie;      // x scattering, y extinction, z scale height, w g
    vec4 ozone;    // xyz absorption, w center
    vec4 radii;    // x bottom, y top, z ozone half width, w ground altitude
    vec4 ground;   // xyz albedo, w sun illuminance top
    vec4 sun;      // xyz direction, w angular radius
    u32 transmittance_lut = 0;
    u32 multiscatter_lut = 0;
    u32 skyview_lut = 0;
    u32 pad = 0;
};
static_assert(sizeof(AtmosphereGPU) % 16 == 0);

class Atmosphere {
  public:
    static constexpr u32 kTransW = 256, kTransH = 64, kMsSize = 32;
    static constexpr u32 kSkyViewW = 192, kSkyViewH = 108;

    Atmosphere(const rhi::Device& dev, rhi::BindlessHeap& heap, const AtmosphereParams& params = {});
    ~Atmosphere();
    Atmosphere(const Atmosphere&) = delete;
    Atmosphere& operator=(const Atmosphere&) = delete;

    const AtmosphereParams& params() const { return p_; }
    AtmosphereGPU gpu(vec3 sun_dir, u32 skyview_index) const;

    // Transmittance from altitude (m above sea level) towards direction with cosine mu to the zenith.
    vec3 transmittance(f32 altitude, f32 mu) const;
    // Sun illuminance (lux, rgb) reaching the given altitude.
    vec3 sun_illuminance(f32 altitude, vec3 sun_dir) const;
    // Sky radiance (cd/m^2) seen from altitude towards dir (ray-marched, single + multiple scattering).
    vec3 sky_radiance(f32 altitude, vec3 dir, vec3 sun_dir, u32 steps = 24) const;
    // Equirect of the sky (+ approximate ground) for image-based lighting, in cd/m^2.
    ImageRGBA32F render_equirect(vec3 sun_dir, u32 width) const;

  private:
    void build_transmittance();
    void build_multiscatter();
    vec3 multiscatter(f32 altitude_from_center, f32 mu_sun) const;
    void scattering(f32 h, vec3& rayleigh, f32& mie_s, vec3& extinction) const;

    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    AtmosphereParams p_;
    std::vector<vec3> trans_, ms_;
    rhi::Image trans_img_, ms_img_;
    u32 trans_index_ = ~0u, ms_index_ = ~0u;
};

} // namespace tefra::render
