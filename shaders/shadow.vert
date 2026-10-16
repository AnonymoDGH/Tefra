#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; InstanceBuffer instances; MaterialBuffer materials; uint cascade;
} pc;
void main() {
    Instance inst = pc.instances.i[gl_InstanceIndex];
    vec4 p = vec4(pc.vertices.v[gl_VertexIndex].pos, 1.0);
    vec3 wp = vec3(dot(inst.model_rows[0], p), dot(inst.model_rows[1], p), dot(inst.model_rows[2], p));
    // Cascades 0..3 are the sun; 4.. are the shadowed local lights (perspective, standard depth).
    mat4 vp = pc.cascade < 4u ? pc.frame.f.cascade_view_proj[pc.cascade] : pc.frame.f.local_shadow_vp[pc.cascade - 4u];
    gl_Position = vp * vec4(wp, 1.0);
}
