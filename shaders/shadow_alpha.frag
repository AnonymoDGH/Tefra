#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/vegetation.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; VegInstanceBuffer instances; MaterialBuffer materials;
    IndexListBuffer visible; uint material; float wind; uint cascade; uint pad;
} pc;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_material;
void main() {
    Material m = pc.materials.m[v_material];
    if (m.emissive_cutoff.w > 0.0 && texture(TEX2D(m.base_color_tex, SMP_ANISO_REPEAT), v_uv).a * m.base_color.a < m.emissive_cutoff.w)
        discard;
}
