#pragma once

#include <utility>
#include <vector>

#include "tefra/core/math.h"

namespace tefra::render {

class Scene;

struct GltfLoadOptions {
    dvec3 position{}; // where the asset's origin is placed in the world
    quat rotation{};
    f32 scale = 1.0f;
    // Heavy meshes (photogrammetry scans): simplify each primitive to at most this many triangles (0 = keep), and
    // also build a coarser LOD with lod1_triangles (0 = none; see GltfLoadResult::lod1).
    u32 max_triangles = 0;
    u32 lod1_triangles = 0;
};

struct GltfLoadResult {
    bool ok = false;
    u32 meshes = 0, materials = 0, textures = 0, instances = 0;
    u32 skins = 0; // skinned primitives get Scene skins (Instance::skin); their palettes start at the rest pose
    u64 triangles = 0;
    std::vector<std::pair<u32, u32>> lod1; // (scene mesh, its coarser LOD mesh) when lod1_triangles > 0
};

// Loads a .gltf/.glb (glTF 2.0) into the scene: meshes, PBR metal-rough materials, textures and node instances.
GltfLoadResult load_gltf(Scene& scene, const char* path, const GltfLoadOptions& options);

} // namespace tefra::render
