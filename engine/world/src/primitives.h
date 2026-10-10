#pragma once
#include "tefra/renderer/mesh.h"
namespace tefra::world {
// Unit primitives (fit the 1x1x1 box centred at the origin; scaled by the part's Size).
render::MeshData make_unit_cylinder(u32 segments = 48); // axis along +Y
render::MeshData make_unit_wedge();                     // slope rising from the front (-Z) bottom to the back top
} // namespace tefra::world
