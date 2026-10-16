#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "tefra/core/math.h"

namespace tefra::render {

class Scene;
class GltfPrepared; // CPU-side result of gltf_prepare (defined in gltf.cpp)

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

// The load split in two (the rock scans take seconds of CPU each, the GPU uploads must stay on one thread):
//  - gltf_prepare parses the file and builds its meshes (attribute decode, simplification). It touches no GPU object,
//    so it runs on any thread, and independent files may be prepared in parallel. Static meshes are cached as compact
//    binaries under config_dir()/cache/gltf, keyed by path, file size, mtime and the options; a later load of an
//    unchanged file skips the decode and the simplification. Returns null when the file cannot be parsed.
//  - gltf_commit uploads the prepared file into the scene (textures, materials, meshes, instances). Main thread only.
std::shared_ptr<GltfPrepared> gltf_prepare(const char* path, const GltfLoadOptions& options);
GltfLoadResult gltf_commit(Scene& scene, GltfPrepared& prepared);

} // namespace tefra::render
