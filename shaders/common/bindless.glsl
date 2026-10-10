#ifndef TEFRA_BINDLESS_GLSL
#define TEFRA_BINDLESS_GLSL
// Set 0: global bindless heap (rhi/bindless.h). Indices are dynamically uniform per draw/dispatch.
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D g_textures[];
layout(set = 0, binding = 1) uniform textureCube g_cubes[];
layout(set = 0, binding = 2) uniform sampler g_samplers[8];

#define SMP_LINEAR_REPEAT 0
#define SMP_LINEAR_CLAMP 1
#define SMP_NEAREST_CLAMP 2
#define SMP_ANISO_REPEAT 3
#define SMP_SHADOW 4
#define SMP_LINEAR_CLAMP_MIP 5
#define SMP_NEAREST_REPEAT 6
#define SMP_ANISO_CLAMP 7

#define TEX2D(idx, smp) sampler2D(g_textures[idx], g_samplers[smp])
#define TEXCUBE(idx, smp) samplerCube(g_cubes[idx], g_samplers[smp])
#endif
