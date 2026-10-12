#pragma once

// Model import (Studio's "Import 3D Model", MCP import_model): converts FBX, OBJ (+MTL), STL and PLY into a binary
// glTF 2.0 (.glb) that the regular glTF loader (load_gltf) reads. Geometry, normals, UVs, optional tangents and
// vertex colors (PLY), PBR materials with PNG/JPEG textures, and the node hierarchy (preserved, with local transforms).
// Output is in metres, +Y up, front +Z (glTF; ADR-009 engine axes). Skinned meshes are written in their bind pose.

#include <string>
#include <vector>

#include "tefra/core/types.h"

namespace tefra::render {

enum class ModelUpAxis : u8 {
    Auto, // by format: Z for STL and PLY (CAD, scanners), Y for OBJ. FBX carries its own axes.
    Y,
    Z,
};

struct ModelImportOptions {
    // Multiplies the positions of formats without unit metadata (OBJ, STL, PLY): 1 = the file is in metres, 0.01 = cm,
    // 0.0254 = inches. FBX units come from the file (ufbx converts them to metres).
    f32 unit_scale = 1.0f;
    ModelUpAxis source_up = ModelUpAxis::Auto; // OBJ, STL and PLY; FBX is converted by its own axes
    // Simplify each primitive to at most this many triangles (0 = keep all). Uses meshoptimizer.
    u32 max_triangles = 0;
    bool write_tangents = false; // TANGENT from the UVs (normal maps); the engine computes them when absent
    // true: textures are embedded in the .glb (default). false: they are written next to it as <stem>_tex<N>.<ext>.
    bool embed_textures = true;
};

struct ModelImportReport {
    std::string format; // "fbx", "obj", "stl", "ply"
    u32 nodes = 0;
    u32 meshes = 0;
    u32 primitives = 0;
    u32 materials = 0;
    u32 textures = 0;
    u32 skinned_meshes = 0; // written in their bind pose: no skeleton is exported
    u64 triangles_in = 0;
    u64 triangles_out = 0;
    std::vector<std::string> warnings;
};

// Lower-case extension of a model file this importer reads: "fbx", "obj", "stl", "ply", or "" when unsupported.
std::string model_import_format(const std::string& path);

// Converts src (any supported format) into dst_glb (.glb). On failure returns false and fills *error.
bool convert_to_glb(const char* src, const char* dst_glb, const ModelImportOptions& options, std::string* error,
                    ModelImportReport* report);

} // namespace tefra::render
