#ifndef TEFRA_TERRAIN_DATA_GLSL
#define TEFRA_TERRAIN_DATA_GLSL
// Terrain buffer types and heightmap access (no push constants: shared by terrain and grass shaders).
#include "common/bindless.glsl"
struct TerrainData {
    vec4 extent; vec4 height;
    uint heightmap; uint mask;
    uint albedo0; uint albedo1; uint albedo2; uint albedo3;
    uint normal0; uint normal1; uint normal2; uint normal3;
    uint arm0; uint arm1; uint arm2; uint arm3;
    uint flags; uint shore; // flags bit 0: terrain detail path (terrain.frag), bit 1: coast look; shore: R16 signed shore distance
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer TerrainBuffer { TerrainData t; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Vec4Buffer { vec4 v[]; };

vec2 terrain_uv(TerrainData t, vec2 world_xz) { return (world_xz - t.extent.xy) * t.extent.w; }
float terrain_height(TerrainData t, vec2 world_xz) {
    return t.height.x + t.height.y * textureLod(TEX2D(t.heightmap, SMP_LINEAR_CLAMP), terrain_uv(t, world_xz), 0.0).r;
}
#endif
