#pragma once

// Procedural coconut palm (Cocos nucifera), clean-room. The trunk is a curved ring grid with a base bulb, bark UVs
// (Poly Haven palm_tree_bark) and a leaning axis. The crown is 14-22 pinnate fronds: each frond is two V-folded
// half-card strips that sample a baked leaflet atlas (make_frond_textures), plus a few coconuts under the crown.

#include "tefra/renderer/mesh.h"

#include <vector>

namespace tefra::render {

struct PalmMeshes {
    MeshData trunk_lod0, crown_lod0, trunk_lod1, crown_lod1;
    f32 height = 0.0f;
    f32 crown_radius = 0.0f;  // m: horizontal reach of the fronds around the trunk axis
    f32 canopy_radius = 0.0f; // m: footprint radius of the crown seen from above (canopy occlusion)
};

// The trunk leans towards +X at its top (the scene rotates the instance to aim it). The seed sets the lean
// amount (5-15 deg at the top), trunk radius, frond count and layout. Height in metres.
// Crown LOD0 and trunk LOD0 include the coconuts (bark material); LOD1 has the same fronds on single-station cards.
PalmMeshes make_palm(u32 seed, f32 height);

// Baked frond atlas, width x height = 1024 x 512: 2x2 tiles of 512x256 (tile = row * 2 + column). Tiles 0-2 are living
// frond variants, tile 3 a dead frond. Along the tile's U runs the frond (0 = base, 1 = tip); in V the midrib is at
// 0.5 and leaflets fan out to both edges. Albedo RGBA8 sRGB (alpha = leaflet coverage, alpha-test with ~0.35).
// Normal RGBA8: tangent-space normal in RGB (G along +V, the bitangent of the frond cards), depth AO in A (flag 8).
void make_frond_textures(u32 seed, u32 width, u32 height, std::vector<u8>& albedo, std::vector<u8>& normal);

} // namespace tefra::render
