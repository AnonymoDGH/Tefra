#pragma once

// Canopy occlusion map (sky visibility under forest). A camera-centred 512 m square at 1 m texels holds, per texel,
// the canopy cover above the ground (R16) and the canopy top height (G16). It is built on the CPU from the static
// vegetation footprints (Vegetation::collect_canopy), blurred, and uploaded once per frame slot when stale.
// lighting.frag attenuates the ambient light by the cover above each pixel's height.

#include "tefra/core/math.h"
#include "tefra/rhi/resources.h"

#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {

class Vegetation;

inline constexpr u32 kCanopyRes = 512;        // texels per side
inline constexpr f32 kCanopyTexelSize = 1.0f; // m
// Encoding of the G channel (top height): h = base + g * range. Must match lighting.frag.
inline constexpr f32 kCanopyYBase = -100.0f; // m
inline constexpr f32 kCanopyYRange = 800.0f; // m

struct CanopyBinding {
    u32 index = 0xFFFFFFFFu; // bindless 2D index (~0u = no map)
    vec2 origin{};           // world xz of the min corner (m)
    f32 size = 0.0f;         // m
};

class CanopyMap {
  public:
    CanopyMap(const rhi::Device& dev, rhi::BindlessHeap& heap, u32 slots);
    ~CanopyMap();
    CanopyMap(const CanopyMap&) = delete;
    CanopyMap& operator=(const CanopyMap&) = delete;

    bool supported() const { return supported_; }
    // Rebuilds the CPU data when the camera has moved far from the map centre or the vegetation changed.
    void update(const Vegetation& veg, dvec3 camera_world);
    // Records the upload into cmd if this slot's copy is stale. Returns the binding valid for this slot.
    CanopyBinding sync(VkCommandBuffer cmd, u32 slot);

  private:
    struct Slot {
        rhi::Image image;
        rhi::Buffer staging;
        u32 bindless = 0xFFFFFFFFu;
        u32 version = 0; // 0 = never uploaded
    };
    const rhi::Device& dev_;
    rhi::BindlessHeap& heap_;
    std::vector<Slot> slots_;
    std::vector<u16> cpu_;             // RG16_UNORM texels, row-major
    std::vector<f32> tau_, top_, tmp_; // scratch (CPU rebuild)
    vec2 origin_{};
    f64 center_x_ = 0.0, center_z_ = 0.0; // camera position at the last rebuild
    u64 veg_uid_ = 0; // Vegetation::uid() of the last rebuild
    u32 version_ = 0;
    bool built_ = false;
    bool supported_ = false;
};

} // namespace tefra::render
