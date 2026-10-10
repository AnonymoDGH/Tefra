#pragma once

#include "tefra/renderer/gpu_types.h"

#include <vector>

namespace tefra::render {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<u32> indices; // triangle list, counter-clockwise front faces
};

// Octahedral unit-vector encoding into snorm16x2 (x in low 16 bits), matching GLSL unpackSnorm2x16.
u32 oct_encode(vec3 n);
vec3 oct_decode(u32 packed);

Vertex make_vertex(vec3 pos, vec3 normal, vec2 uv, vec3 tangent, f32 tangent_sign);

// Procedural meshes (meters). UVs: 1 unit per `uv_scale` meters where applicable.
MeshData make_sphere(f32 radius, u32 segments = 64, u32 rings = 32);
MeshData make_plane(f32 size_x, f32 size_z, f32 uv_scale = 1.0f); // y = 0, normal +Y
MeshData make_box(vec3 half_extents);

// Per-vertex tangents from UVs (accumulated per triangle, Gram-Schmidt). For meshes without tangents.
void compute_tangents(MeshData& mesh);

struct Bounds {
    vec3 center;
    f32 radius;
};
Bounds compute_bounds(const MeshData& mesh);

} // namespace tefra::render
