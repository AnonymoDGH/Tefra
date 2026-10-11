#include "common/gpu_types.glsl"
#include "common/vegetation.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; VegInstanceBuffer instances; MaterialBuffer materials;
    IndexListBuffer visible; uint material; float wind; uint cascade; uint pad;
} pc;
layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec4 v_tangent;
layout(location = 3) out vec2 v_uv;
layout(location = 4) flat out uint v_material;
#ifndef SHADOW_PASS
layout(location = 5) flat out float v_rand; // per-instance random: bark tint and moss
#ifndef SHADOW_PASS
// The G-buffer fragment shader reads these for motion vectors. Vegetation does not move in the G-buffer pass
// (wind is not part of the velocity yet), so its velocity is zero.
layout(location = 6) out vec4 v_cur_clip;
layout(location = 7) out vec4 v_prev_clip;
layout(location = 8) flat out uint v_dynamic;
#endif
#endif
void main() {
    Frame f = pc.frame.f;
    VegInstance inst = pc.instances.i[pc.visible.v[gl_InstanceIndex]];
    Vertex vtx = pc.vertices.v[gl_VertexIndex];
    float s = inst.pos_scale.w, sn = inst.rot_proto.x, cs = inst.rot_proto.y;
    vec3 lp = vtx.pos * s;
    vec3 p = vec3(cs * lp.x - sn * lp.z, lp.y, sn * lp.x + cs * lp.z);
    // Wind: height-weighted sway with per-instance phase.
    if (pc.wind > 0.0) {
        float t = f.camera_pos.w;
        float phase = inst.rot_proto.w * 6.2831 + inst.pos_scale.x * 0.03;
        float h = max(lp.y, 0.0);
        float bend = pc.wind * pow(h, 1.5) * 0.0015;
        p.x += bend * (sin(t * 1.1 + phase) + 0.4 * sin(t * 2.7 + phase * 1.7));
        p.z += bend * 0.6 * sin(t * 0.9 + phase * 1.3);
    }
    vec3 n = oct_decode(vtx.normal), tg = oct_decode(vtx.tangent);
    v_normal = vec3(cs * n.x - sn * n.z, n.y, sn * n.x + cs * n.z);
    v_tangent = vec4(cs * tg.x - sn * tg.z, tg.y, sn * tg.x + cs * tg.z, vtx.tangent_sign);
    v_pos = inst.pos_scale.xyz + p - f.camera_pos.xyz;
    v_uv = vtx.uv;
    v_material = pc.material;
#ifndef SHADOW_PASS
    v_rand = inst.rot_proto.w;
#endif
#ifdef SHADOW_PASS
    gl_Position = f.cascade_view_proj[pc.cascade] * vec4(v_pos, 1.0);
#else
    gl_Position = f.view_proj * vec4(v_pos, 1.0);
    v_cur_clip = gl_Position;
    v_prev_clip = gl_Position;
    v_dynamic = 0u;
#endif
}
