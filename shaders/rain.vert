#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
// Rain streaks (Lighting.Rain): procedural drops wrapped in a box around the camera, no buffers. Each streak is a thin
// quad along the drop velocity (fall + wind), as long as the distance a drop travels during a ~1/30 s exposure, lit
// by the nearest local lights with a strong forward lobe (drops shine against street lights).
layout(push_constant) uniform Push { FrameBuffer frame; LightBuffer lights; uint depth; uint light_count; uint drops; uint pad; } pc;
layout(location = 0) out vec2 v_uv;      // x across (-1..1), y along (0 top .. 1 bottom)
layout(location = 1) out vec3 v_color;   // pre-exposed radiance
layout(location = 2) out float v_alpha;

const vec3 kBox = vec3(30.0, 20.0, 30.0);
const vec2 kCorners[6] = vec2[](vec2(-1, 0), vec2(1, 0), vec2(1, 1), vec2(-1, 0), vec2(1, 1), vec2(-1, 1));

vec4 hash41(uint n) {
    uvec4 v = uvec4(n) * uvec4(1597334677u, 3812015801u, 2798796415u, 1979697957u);
    v = (v.yzwx ^ (v >> 16u)) * 1103515245u;
    return vec4(v & 0xFFFFFFu) / 16777216.0;
}

void main() {
    Frame f = pc.frame.f;
    uint id = uint(gl_VertexIndex) / 6u;
    vec2 c = kCorners[uint(gl_VertexIndex) % 6u];
    vec4 h = hash41(id);
    float speed = f.weather.w * (0.85 + 0.3 * h.w);
    vec3 vel = vec3(f.rain_wind.x, -speed, f.rain_wind.z) + vec3(h.x - 0.5, 0.0, h.z - 0.5) * 0.6;
    float t = f.camera_pos.w;
    // World position of this drop, wrapped into the box around the camera (camera-relative result).
    vec3 p = h.xyz * kBox + vel * t - f.camera_pos.xyz;
    p = mod(p + kBox * 0.5, kBox) - kBox * 0.5;
    vec3 axis = normalize(vel);
    float len = f.rain_wind.w * (speed / 9.0);
    float dist = length(p);
    // Thin but at least ~1 px wide (dimmed accordingly).
    float px = dist * 2.0 / (abs(f.proj[1][1]) * f.viewport.y);
    float width = 0.0035;
    float dim = 1.0;
    if (width < px * 0.7) {
        dim = width / (px * 0.7);
        width = px * 0.7;
    }
    vec3 side = normalize(cross(axis, p + vec3(1e-4)));
    vec3 pos = p - axis * (len * c.y) + side * (width * c.x);
    // Light: each nearby light, scattered mostly forward (towards a viewer looking at the light through the rain).
    vec3 V = -normalize(p);
    vec3 col = f.ambient.xyz * 0.02 + vec3(0.02, 0.025, 0.03) * f.sun_color.rgb * f.sun_dir.w * 0.0002;
    for (uint i = 0u; i < pc.light_count; ++i) {
        LightGPU l = pc.lights.l[i];
        vec3 d = l.pos_range.xyz - p;
        float d2 = dot(d, d), r = l.pos_range.w;
        if (d2 > r * r) continue;
        float win = clamp(1.0 - d2 * d2 / (r * r * r * r), 0.0, 1.0);
        float fwd = pow(max(dot(normalize(d), -V), 0.0), 8.0);
        col += l.color.rgb * win * win / max(d2, 0.2) * (0.06 + 1.4 * fwd);
    }
    v_color = col * f.exposure.x;
    v_alpha = f.weather.z * dim * smoothstep(0.4, 1.5, dist) * (1.0 - smoothstep(10.0, 15.0, dist));
    v_uv = c;
    gl_Position = f.view_proj * vec4(pos, 1.0);
}
