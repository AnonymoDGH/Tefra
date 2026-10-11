#pragma once

// River ribbon + lake surface (spec 2 / M5 preview): dark, turbid water rendered in a forward pass over the lit
// HDR image. Absorption from the depth below the surface, Fresnel reflection of the environment, procedural ripples.

#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

namespace tefra::rhi {
class Device;
} // namespace tefra::rhi

namespace tefra::render {

class Terrain;

class Water {
  public:
    Water(const rhi::Device& dev, const Terrain& terrain);
    ~Water();
    Water(const Water&) = delete;
    Water& operator=(const Water&) = delete;

    const rhi::Buffer& vertices() const { return vb_; } // vec4: world xyz, w = flow speed (m/s)
    const rhi::Buffer& indices() const { return ib_; }
    u32 index_count() const { return index_count_; }
    f32 level_at(f32 x, f32 z) const;

  private:
    const rhi::Device& dev_;
    const Terrain& terrain_;
    rhi::Buffer vb_, ib_;
    u32 index_count_ = 0;
};

} // namespace tefra::render
