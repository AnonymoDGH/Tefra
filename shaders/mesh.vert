#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; InstanceBuffer instances; MaterialBuffer materials; uint cascade;
} pc;
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;
layout(location = 3) out vec2 v_uv;
layout(location = 4) flat out uint v_material;
layout(location = 5) flat out float v_rand;
void main() {
    Instance inst = pc.instances.i[gl_InstanceIndex];
    Vertex vtx = pc.vertices.v[gl_VertexIndex];
    vec4 p = vec4(vtx.pos, 1.0);
    vec3 wp = vec3(dot(inst.model_rows[0], p), dot(inst.model_rows[1], p), dot(inst.model_rows[2], p));
    vec3 n = oct_decode(vtx.normal), t = oct_decode(vtx.tangent);
    v_normal = normalize(vec3(dot(inst.normal_rows[0].xyz, n), dot(inst.normal_rows[1].xyz, n), dot(inst.normal_rows[2].xyz, n)));
    v_tangent = vec4(normalize(vec3(dot(inst.model_rows[0].xyz, t), dot(inst.model_rows[1].xyz, t), dot(inst.model_rows[2].xyz, t))), vtx.tangent_sign);
    v_pos = wp;
    v_uv = vtx.uv;
    v_material = inst.material;
    v_rand = 0.0;
    gl_Position = pc.frame.f.view_proj * vec4(wp, 1.0);
}
