#pragma once

// Vocabulary of the spatial queries shared by physics and scripting (Roblox RaycastParams / OverlapParams).

#include "tefra/core/math.h"
#include "tefra/core/types.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <vector>

namespace tefra::world {

// Instance filter (FilterDescendantsInstances + FilterType). A part is filtered by its ancestry: the part itself or
// one of its ancestors must be listed. Exclude (the default) ignores listed parts; Include hits only listed parts,
// so an Include filter with an empty list hits nothing. An Exclude filter with an empty list filters nothing.
struct QueryFilter {
    std::vector<const Instance*> instances;
    bool include = false;

    bool accepts(const Instance* part) const {
        if (!include && instances.empty()) return true;
        bool listed = false;
        for (const Instance* p = part; p && !listed; p = p->parent())
            listed = std::find(instances.begin(), instances.end(), p) != instances.end();
        return include ? listed : !listed;
    }
};

enum class OverlapKind : u32 {
    Radius, // sphere: center + radius
    Box,    // oriented box: center + orientation (degrees, Y, then X, then Z) + size
    Part    // the oriented box of `part` (the part itself is never reported)
};

// Overlap query (Workspace:GetPartBoundsInRadius / GetPartBoundsInBox / GetPartsInPart).
struct OverlapRequest {
    OverlapKind kind = OverlapKind::Radius;
    vec3 center{};
    vec3 orientation{};                  // degrees, Box only
    vec3 size{};                         // metres, Box only
    f32 radius = 0.0f;                   // Radius only
    const Instance* part = nullptr;      // Part only
    const QueryFilter* filter = nullptr; // nullptr = no filter
    u32 max_parts = 0;                   // OverlapParams.MaxParts; 0 = no limit
};

} // namespace tefra::world
