#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/ocean.glsl"
layout(location = 0) out vec3 v_pos;   // camera-relative displaced position
layout(location = 1) out vec2 v_xz;    // still-surface world xz (the wave functions' domain)
layout(location = 2) out vec4 v_shore; // x depth, y shore distance, zw to-shore direction
void main() {
    Frame f = pc.frame.f;
    vec4 g = pc.verts.v[gl_VertexIndex];
    vec2 xz = pc.params.zw + g.xy;
    Shore sh = ocean_shore(xz);
    // Far rings: waves shorter than ~4 cells alias; their height fades with the ring radius.
    OceanSample o = ocean_eval(xz, pc.time, sh, false, 0.0);
    float fade = 1.0 - smoothstep(600.0, 3000.0, g.z);
    vec3 world = vec3(xz.x, pc.params.x, xz.y) + o.disp * fade;
    v_pos = world - f.camera_pos.xyz;
    v_xz = xz;
    v_shore = vec4(sh.depth, sh.dist, sh.to_shore);
    gl_Position = f.view_proj * vec4(v_pos, 1.0);
}
