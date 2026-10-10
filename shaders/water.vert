#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer WaterVerts { vec4 v[]; };
layout(push_constant) uniform Push { FrameBuffer frame; WaterVerts verts; uint depth; } pc;
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec2 v_world;
layout(location = 2) out float v_flow;
void main() {
    vec4 w = pc.verts.v[gl_VertexIndex];
    Frame f = pc.frame.f;
    v_pos = w.xyz - f.camera_pos.xyz;
    v_world = w.xz;
    v_flow = w.w;
    gl_Position = f.view_proj * vec4(v_pos, 1.0);
}
