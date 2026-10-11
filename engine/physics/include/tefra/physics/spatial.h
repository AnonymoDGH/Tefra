#pragma once

// Geometry of the spatial queries (Workspace:GetPartBoundsInRadius / GetPartBoundsInBox / GetPartsInPart) and part
// mass. Plain math on the DataModel, no Jolt: a part is its oriented box (Position, Orientation in degrees Y, X, Z,
// Size). Ball parts count as the sphere inscribed in their box for radius queries (exact for a ball); every other
// test uses the box, so a ball or cylinder can be reported when only its bounding box touches the query.

#include "tefra/core/math.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/query.h"

#include <vector>

namespace tefra::physics {

struct Obb {
    vec3 center{};
    quat rotation{};
    vec3 half{};       // half extents (m)
    bool ball = false; // radius queries use the sphere inscribed in the box
};

// The oriented box of a BasePart (its Shape enum decides `ball`).
Obb part_obb(const world::Instance& part);

// True when the sphere (center, radius) touches or overlaps the box.
bool sphere_overlaps(const Obb& box, vec3 center, f32 radius);

// Separating-axis test between two boxes (15 axes). Touching faces count as overlap.
bool obb_overlaps(const Obb& a, const Obb& b);

// Volume (m^3) of a primitive of the given Part.Shape (0 Block, 1 Ball, 2 Cylinder, 3 Wedge) and size.
f32 part_volume(u32 shape, vec3 size);

// Mass (kg) of a part: Density x volume, as the physics bodies are built. Density is clamped to at least 1 kg/m^3.
f32 part_mass(const world::Instance& part);

// Parts under the Workspace that match the query, in DataModel order (at most max_parts when set). Parts that the
// filter rejects are skipped. `out` is cleared first. Does nothing when the model has no Workspace.
void find_parts(const world::DataModel& model, const world::OverlapRequest& query, std::vector<world::Instance*>& out);

} // namespace tefra::physics
