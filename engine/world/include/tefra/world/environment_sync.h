#pragma once

// Landscape of a DataModel (M3b): the Workspace Terrain (Kind Valley or Coast, Seed, SeaLevel, Vegetation) and the
// Ocean become the GPU terrain, the trees / palms / rocks, the water or ocean surface, and the CPU heightfield that
// physics, templates and the runtime share with them. The same builders produce tefra_bench's "valley" and "coast"
// scenes, so a place with a Terrain looks like the bench scene it was copied from.
//
// Cost: the first generation of a field takes 1-4 s (TerrainField::acquire caches the last two). EnvironmentSync
// rebuilds only when a Terrain or Ocean property changes; it waits for the GPU to be idle first. Rebuilding adds
// meshes and materials to the Scene (the Scene has no removal yet), so every rebuild leaks a few MB of GPU memory
// until a restart: tracked in docs/research/m3b-landscape/REPORT.md.

#include "tefra/core/math.h"
#include "tefra/renderer/terrain_field.h"
#include "tefra/world/datamodel.h"

#include <memory>
#include <string>

namespace tefra::rhi {
class Device;
class BindlessHeap;
} // namespace tefra::rhi

namespace tefra::render {
class Scene;
class Renderer;
class Terrain;
class Vegetation;
class Water;
class Ocean;
} // namespace tefra::render

namespace tefra::world {

// ---- CPU side (no GPU) -------------------------------------------------------------------------------------------

// The first Terrain child of the Workspace with Kind != None. nullptr when there is none.
Instance* find_terrain(const DataModel& dm);
// The Ocean child of the Workspace, or nullptr.
Instance* find_ocean(const DataModel& dm);
// Generator settings of a Terrain instance (Kind, Seed, SeaLevel). False when Kind is None.
bool terrain_desc(const Instance& terrain, render::TerrainDesc& out);
// Height (m) of the landscape at (x, z): the same values as the GPU terrain and the physics heightfield. 0 when the
// instance has Kind None. Generates the field on first use (cached by TerrainField::acquire).
f32 terrain_height(const Instance& terrain, f32 x, f32 z);

// ---- Building blocks shared with tefra_bench (apps/common/scenes.cpp) --------------------------------------------

// Valley materials, conifer and undergrowth prototypes and the forest scatter. Returns the start point (x, z) beside
// the river. cache_dir holds the CC0 assets (textures/, models/); missing files are skipped.
vec2 populate_valley(render::Scene& scene, const render::Terrain& terrain, render::Vegetation& veg,
                     const std::string& cache_dir = "assets/cache");
// Coast: palms on the back beach, reef / pebble / boulder rocks, the scatter. Returns the x, z of a beach spot
// ~20 m from the water (the camera of the bench scene stands there).
vec2 populate_coast(render::Scene& scene, const render::Terrain& terrain, render::Vegetation& veg,
                    const std::string& cache_dir = "assets/cache");
// Three procedural palm prototypes (bark and frond materials). palms[] receives the prototype indices.
void add_palm_prototypes(render::Scene& scene, render::Vegetation& veg, u32 palms[3],
                         const std::string& cache_dir = "assets/cache");

// ---- Live landscape ----------------------------------------------------------------------------------------------

class EnvironmentSync {
  public:
    // The renderer draws the landscape through Renderer::set_landscape; it must outlive this object.
    EnvironmentSync(const rhi::Device& dev, rhi::BindlessHeap& heap, render::Scene& scene, render::Renderer& renderer);
    ~EnvironmentSync();
    EnvironmentSync(const EnvironmentSync&) = delete;
    EnvironmentSync& operator=(const EnvironmentSync&) = delete;

    // Call once per frame on the main thread. Rebuilds what changed in the model and returns true when anything was
    // rebuilt (the caller may refresh lighting that depends on the landscape).
    bool update(const DataModel& model);
    // Detaches the landscape from the renderer and frees its GPU objects (before the device or heap go away).
    void clear();

    bool has_terrain() const;
    bool has_ocean() const;
    render::TerrainKind kind() const; // Valley / Coast; Valley when there is no terrain
    f32 sea_level() const;            // 0 without a terrain
    // Height (m) of the landscape at (x, z); 0 without a terrain.
    f32 height_at(f32 x, f32 z) const;
    // The shared CPU field (null without a terrain).
    const render::TerrainField* field() const;
    // Counts rebuilds of the terrain (0 = none yet); changes whenever the GPU terrain is replaced.
    u64 terrain_revision() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tefra::world
