#pragma once

// Ocean surface (coast scene): a camera-centred polar grid displaced by a sum of Gerstner waves (open sea) and a
// shore-driven surf system (crests travelling up the shore-distance field, shoaling, breaking bores and swash).
// Rendered after the lighting pass: the lit image is copied and the water samples it for refraction, so the sea
// floor gets per-channel absorption (turquoise shallows) instead of a grey blend.

#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

#include <vector>

namespace tefra::rhi {
class Device;
} // namespace tefra::rhi

namespace tefra::render {

// One Gerstner component (std430, 32 bytes).
struct OceanWaveGPU {
    vec4 dir_k; // xy = travel direction (world xz, unit), z = wavenumber k (rad/m), w = angular frequency (rad/s)
    vec4 amp;   // x = amplitude (m), y = steepness Q (0..1), z = phase (rad), w = unused
};
static_assert(sizeof(OceanWaveGPU) == 32);

struct OceanDesc {
    f32 level = 0.0f;          // world y of the still surface
    vec2 wind_dir{0.0f, 1.0f}; // direction the swell travels (world xz); +Z = towards the coast scene's beach
    f32 swell_height = 0.9f;   // significant height of the open-sea swell (m)
    f32 surf_height = 0.7f;    // height of the breaking waves at the shore (m)
    vec3 absorption{0.46f, 0.075f, 0.045f};  // 1/m: clear tropical water (red goes first)
    vec3 scattering{0.004f, 0.016f, 0.022f}; // 1/m: in-scatter colour of the water body
};

class Ocean {
  public:
    static constexpr u32 kSectors = 24; // angular sectors of the grid, culled against the view on the CPU
    static constexpr u32 kGeometryWaves = 10;
    static constexpr u32 kDetailWaves = 14;

    Ocean(const rhi::Device& dev, const OceanDesc& desc);
    ~Ocean();
    Ocean(const Ocean&) = delete;
    Ocean& operator=(const Ocean&) = delete;

    const OceanDesc& desc() const { return desc_; }
    const rhi::Buffer& vertices() const { return vb_; } // vec4: x, z offset from the camera (m), y = ring radius
    const rhi::Buffer& indices() const { return ib_; }
    const rhi::Buffer& waves() const { return waves_; } // kGeometryWaves + kDetailWaves OceanWaveGPU
    // Index range of sector s (draw it when its angular span faces the camera).
    u32 sector_first(u32 s) const { return s * sector_indices_; }
    u32 sector_count() const { return sector_indices_; }
    f32 sector_angle(u32 s) const { return (f32(s) + 0.5f) * kTwoPi / f32(kSectors); } // centre (rad, atan2(z, x))

  private:
    void build_grid();
    void build_waves();

    const rhi::Device& dev_;
    OceanDesc desc_;
    rhi::Buffer vb_, ib_, waves_;
    u32 sector_indices_ = 0;
};

} // namespace tefra::render
