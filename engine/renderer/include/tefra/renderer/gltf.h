#pragma once

#include "tefra/core/math.h"

namespace tefra::render {

class Scene;

struct GltfLoadOptions {
    dvec3 position{};  // where the asset's origin is placed in the world
    quat rotation{};
    f32 scale = 1.0f;
};

struct GltfLoadResult {
    bool ok = false;
    u32 meshes = 0, materials = 0, textures = 0, instances = 0;
    u64 triangles = 0;
};

// Loads a .gltf/.glb (glTF 2.0) into the scene: meshes, PBR metal-rough materials, textures and node instances.
GltfLoadResult load_gltf(Scene& scene, const char* path, const GltfLoadOptions& options);

} // namespace tefra::render
