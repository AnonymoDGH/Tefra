#pragma once

#include "tefra/renderer/atmosphere.h"
#include "tefra/renderer/environment.h"
#include "tefra/renderer/ocean.h"
#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"
#include "tefra/renderer/terrain.h"
#include "tefra/renderer/vegetation.h"
#include "tefra/renderer/water.h"

#include <string>

namespace tefra::app {

struct SceneSetup {
    render::Camera camera;
    render::RenderSettings settings;
    bool use_atmosphere = false;
    bool use_terrain = false;
};

// "valley": the vertical slice environment (terrain, atmosphere, morning mist), camera at ground level.
void build_valley_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                        const render::Terrain& terrain, render::Vegetation& vegetation, SceneSetup& setup);

// "palms": a grove of procedural coconut palms on sand under the physical atmosphere.
void build_palms_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                       render::Vegetation& vegetation, SceneSetup& setup);
// "coast": a sandy cove between rocky headlands, palms on the back beach, the ocean in front (-Z). Needs a Coast
// terrain; the runner adds the ocean surface.
void build_coast_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                       const render::Terrain& terrain, render::Vegetation& vegetation, SceneSetup& setup);
// Builds the IBL environment from the atmosphere for the current sun direction.
void build_atmosphere_environment(const render::Atmosphere& atmo, render::Environment& env, vec3 sun_dir);
// "materials_sky": the materials scene under the physical atmosphere with morning valley fog.
void build_materials_sky_scene(render::Scene& scene, render::Environment& env, const render::Atmosphere& atmo,
                               SceneSetup& setup);

// "materials": 7x5 spheres (roughness by column; dielectric, metal, colored rows) on a checker floor.
void build_materials_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup);
// "gltf": loads a glTF file; environment from an HDR if given, else procedural.
bool build_gltf_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup, const std::string& gltf,
                      const std::string& hdr);

} // namespace tefra::app
