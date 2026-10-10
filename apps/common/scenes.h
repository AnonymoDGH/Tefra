#pragma once

#include "tefra/renderer/environment.h"
#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"

#include <string>

namespace tefra::app {

struct SceneSetup {
    render::Camera camera;
    render::RenderSettings settings;
};

// "materials": 7x5 spheres (roughness by column; dielectric, metal, colored rows) on a checker floor.
void build_materials_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup);
// "gltf": loads a glTF file; environment from an HDR if given, else procedural.
bool build_gltf_scene(render::Scene& scene, render::Environment& env, SceneSetup& setup, const std::string& gltf,
                      const std::string& hdr);

} // namespace tefra::app
