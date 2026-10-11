#ifndef TEFRA_TERRAIN_GLSL
#define TEFRA_TERRAIN_GLSL
#include "common/terrain_data.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; TerrainBuffer terrain; Vec4Buffer patches; Vec4Buffer grid; uint cascade;
} pc;
#endif
