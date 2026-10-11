#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/atmosphere.glsl"
layout(push_constant) uniform Push { FrameBuffer frame; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    Atmosphere a = pc.frame.f.atmo;
    vec3 d = skyview_dir(v_uv, a.sun.xyz);
    float altitude = a.radii.w + max(pc.frame.f.camera_pos.y, 0.0);
    o_color = vec4(atmo_sky(a, altitude, d, a.sun.xyz, 30), 1.0);
}
