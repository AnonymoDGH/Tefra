#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
// Camera-facing particle quads (ParticleGPU, world::ParticleSystem): 6 vertices per particle, no vertex buffer.
struct Particle {
    vec4 pos_size;
    vec4 color;
    vec4 vel_rot;
    uint shape;
    float emission;
    float stretch;
    float brightness;
};
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer Particles { Particle p[]; };
layout(push_constant) uniform Push { FrameBuffer frame; Particles parts; uint depth; } pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_pos;
layout(location = 3) flat out uint v_shape;
layout(location = 4) flat out vec3 v_params; // emission, brightness, size
layout(location = 5) flat out float v_seed;

const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    Frame f = pc.frame.f;
    uint idx = uint(gl_VertexIndex) / 6u;
    Particle pt = pc.parts.p[idx];
    vec2 c = kCorners[uint(gl_VertexIndex) % 6u];
    vec3 center = pt.pos_size.xyz - f.camera_pos.xyz;
    float size = pt.pos_size.w * 0.5;
    // Sub-pixel particles (far sparks, snow): keep them at least ~1.5 px wide and dim them instead, so they do not
    // shimmer or vanish.
    float px = length(center) * 2.0 / (abs(f.proj[1][1]) * f.viewport.y);
    float min_half = 1.5 * px;
    float dim = 1.0;
    if (size < min_half) {
        dim = (size * size) / (min_half * min_half);
        size = min_half;
    }
    // Camera basis from the view rotation (rows of the view matrix).
    vec3 right = vec3(f.view[0][0], f.view[1][0], f.view[2][0]);
    vec3 up = vec3(f.view[0][1], f.view[1][1], f.view[2][1]);
    vec3 fwd = normalize(center);
    vec3 offset;
    vec3 vel = pt.vel_rot.xyz;
    vec3 vs = vel - fwd * dot(vel, fwd); // velocity across the screen
    if (pt.stretch > 0.0 && dot(vs, vs) > 1e-4) {
        // Stretched along the velocity: long axis = screen velocity, short axis across it.
        vec3 ax = normalize(vs);
        vec3 ay = normalize(cross(fwd, ax));
        float len = 1.0 + pt.stretch * length(vs) * 0.1;
        offset = ax * (c.x * size * len) + ay * (c.y * size);
    } else {
        float r = pt.vel_rot.w, cr = cos(r), sr = sin(r);
        vec2 q = vec2(c.x * cr - c.y * sr, c.x * sr + c.y * cr);
        offset = (right * q.x + up * q.y) * size;
    }
    v_pos = center + offset;
    v_uv = c;
    v_color = vec4(pt.color.rgb, pt.color.a * dim);
    v_shape = pt.shape;
    v_params = vec3(pt.emission, pt.brightness, pt.pos_size.w);
    v_seed = fract(float(idx) * 0.61803);
    gl_Position = f.view_proj * vec4(v_pos, 1.0);
}
