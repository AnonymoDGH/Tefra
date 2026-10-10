#pragma once

// CPU reference of the GPU skinning pass (shaders/skin.comp). Tests compare the GPU output against these functions.
// Linear blend skinning: position = sum_i w_i * (palette[j_i] * p), normal and tangent use the upper 3x3 of each
// palette matrix and are renormalized. A vertex whose weights sum to zero keeps its bind pose.

#include "tefra/renderer/mesh.h"

#include <vector>

namespace tefra::render {

Vertex skin_vertex_reference(const Vertex& v, const SkinAttr& s, const std::vector<mat4>& palette);
// Skins every vertex of the mesh (mesh.skin must hold one entry per vertex).
MeshData skin_mesh_reference(const MeshData& mesh, const std::vector<mat4>& palette);

} // namespace tefra::render
