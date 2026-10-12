#pragma once

// Image-based lighting (spec 1 §6): environment cube prefiltered for GGX (split-sum), irradiance SH9 and the
// BRDF LUT. Source: a Radiance .hdr equirect or a procedural sky (for asset-free tests).

#include "tefra/core/image.h"
#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

struct ProceduralSky {
    vec3 sun_direction = normalize(vec3{0.35f, 0.17f, -0.92f}); // towards the sun
    f32 zenith_nits = 3500.0f;                                  // luminance at the zenith (cd/m^2)
    f32 horizon_nits = 6500.0f;                                 // luminance at the horizon (hazy)
    vec3 ground_albedo{0.12f, 0.11f, 0.09f};
    f32 sun_illuminance = 30000.0f; // lux, used for the ground and the glow around the sun
};

struct EnvironmentSettings {
    u32 cube_size = 128;   // base resolution of the prefiltered cube (128 Low, 256 Medium+)
    u32 specular_mips = 6; // roughness 0..1 over these mips
    u32 samples = 128;     // GGX importance samples per texel
};

class Environment {
  public:
    Environment(const rhi::Device& dev, rhi::BindlessHeap& heap, const EnvironmentSettings& settings = {});
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    void build_procedural(const ProceduralSky& sky);
    // Scales the HDR so that the mean luminance of the upper hemisphere equals `sky_nits` (0 = keep values).
    bool build_from_hdr(const char* path, f32 sky_nits);
    // Builds from an equirect already in memory (linear RGB, luminance in cd/m^2).
    void build_from_equirect(const ImageRGBA32F& equirect);

    u32 cube() const { return cube_index_; }
    u32 mips() const { return settings_.specular_mips; }
    u32 brdf_lut() const { return lut_index_; }
    u32 background() const { return background_index_; }
    const vec4* sh() const { return sh_; } // 9 coefficients, cosine-convolved irradiance (rgb)
    // Brightest direction found in the source (sun for most HDRIs) and its estimated illuminance.
    vec3 dominant_direction() const { return dominant_dir_; }
    f32 dominant_illuminance() const { return dominant_lux_; }

    // CPU evaluation of the irradiance SH (tests).
    vec3 irradiance(vec3 n) const;

  private:
    void build_brdf_lut();
    void release_cube();

    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    EnvironmentSettings settings_;
    rhi::Image prefiltered_;
    rhi::Image lut_;
    rhi::Image background_;
    u32 background_index_ = ~0u;
    u32 cube_index_ = ~0u;
    u32 lut_index_ = ~0u;
    vec4 sh_[9] = {};
    vec3 dominant_dir_{0, 1, 0};
    f32 dominant_lux_ = 0.0f;
};

// Exposed for unit tests.
void sh9_project_equirect(const ImageRGBA32F& equirect, vec4 out_sh[9]); // radiance -> SH (not convolved)
void sh9_convolve_cosine(vec4 sh[9]);                                    // radiance SH -> irradiance SH
vec3 sh9_eval(const vec4 sh[9], vec3 n);
vec3 equirect_direction(f32 u, f32 v);                            // u,v in [0,1]
vec2 brdf_lut_integrate(f32 n_dot_v, f32 roughness, u32 samples); // (scale, bias) for F0
// Six LDR faces (Roblox order: Ft -Z, Bk +Z, Lf -X, Rt +X, Up +Y, Dn -Y; sRGB, top row first) resampled to an equirect
// of the given width (height = width / 2), linear values times scale (nits). Empty (width 0) when a face is missing.
ImageRGBA32F equirect_from_cube_faces(const ImageRGBA8 faces[6], u32 width, f32 scale);

} // namespace tefra::render
