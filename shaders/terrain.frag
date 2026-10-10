#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/terrain.glsl"
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec2 v_world;
layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_material;
layout(location = 3) out vec2 o_velocity;

struct Layer { vec3 albedo; vec3 n; vec3 arm; };

Layer sample_planar(uint a, uint n, uint r, vec2 uv) {
    Layer l;
    l.albedo = texture(TEX2D(a, SMP_ANISO_REPEAT), uv).rgb;
    l.n = texture(TEX2D(n, SMP_ANISO_REPEAT), uv).xyz * 2.0 - 1.0;
    l.arm = texture(TEX2D(r, SMP_ANISO_REPEAT), uv).rgb;
    return l;
}
// Two scales blended by a macro noise value: breaks visible tiling at a distance.
Layer sample_layer(uint a, uint n, uint r, vec2 world, float tile, float macro) {
    Layer l0 = sample_planar(a, n, r, world / tile);
    vec2 w2 = mat2(0.8, -0.6, 0.6, 0.8) * world / (tile * 3.7) + 0.37;
    Layer l1 = sample_planar(a, n, r, w2);
    float k = smoothstep(0.3, 0.7, macro);
    Layer l;
    l.albedo = mix(l0.albedo, l1.albedo, k * 0.6);
    l.n = normalize(mix(l0.n, l1.n, k * 0.6));
    l.arm = mix(l0.arm, l1.arm, k * 0.6);
    return l;
}

void main() {
    Frame f = pc.frame.f;
    TerrainData t = pc.terrain.t;
    float e = t.height.z;
    float hl = terrain_height(t, v_world - vec2(e, 0)), hr = terrain_height(t, v_world + vec2(e, 0));
    float hd = terrain_height(t, v_world - vec2(0, e)), hu = terrain_height(t, v_world + vec2(0, e));
    vec3 Ng = normalize(vec3(hl - hr, 2.0 * e, hd - hu));
    vec4 mask = texture(TEX2D(t.mask, SMP_LINEAR_CLAMP_MIP), terrain_uv(t, v_world));
    float wet = mask.r, patchy = mask.g;
    float slope = 1.0 - Ng.y;

    Layer leaves = sample_layer(t.albedo0, t.normal0, t.arm0, v_world, 2.5, patchy);
    Layer mudl = sample_layer(t.albedo1, t.normal1, t.arm1, v_world, 3.0, patchy);
    Layer mud = sample_layer(t.albedo3, t.normal3, t.arm3, v_world, 3.5, patchy);
    // Rock: triplanar.
    vec3 bw = pow(abs(Ng), vec3(4.0));
    bw /= bw.x + bw.y + bw.z;
    vec3 wp = v_pos + f.camera_pos.xyz;
    Layer rx = sample_planar(t.albedo2, t.normal2, t.arm2, wp.zy / 6.0);
    Layer ry = sample_planar(t.albedo2, t.normal2, t.arm2, wp.xz / 6.0);
    Layer rz = sample_planar(t.albedo2, t.normal2, t.arm2, wp.xy / 6.0);
    Layer rock;
    rock.albedo = rx.albedo * bw.x + ry.albedo * bw.y + rz.albedo * bw.z;
    rock.arm = rx.arm * bw.x + ry.arm * bw.y + rz.arm * bw.z;
    // Whiteout-style blend of the three tangent-space normals into world space.
    vec3 nx = vec3(0.0, rx.n.y, rx.n.x) * sign(Ng.x), ny = vec3(ry.n.x, 0.0, -ry.n.y), nz = vec3(rz.n.x, rz.n.y, 0.0) * sign(Ng.z);
    vec3 rock_n = normalize(Ng + (nx * bw.x + ny * bw.y + nz * bw.z));

    float w_rock = smoothstep(0.38, 0.58, slope + (patchy - 0.5) * 0.15);
    float w_mud = wet * (1.0 - w_rock);
    float w_mudl = smoothstep(0.42, 0.68, patchy) * (1.0 - wet) * (1.0 - w_rock);
    float w_leaves = max(1.0 - w_rock - w_mud - w_mudl, 0.0);
    // Height-aware sharpening (albedo luminance as height proxy).
    vec4 hgt = vec4(dot(leaves.albedo, vec3(0.33)), dot(mudl.albedo, vec3(0.33)), dot(rock.albedo, vec3(0.33)), dot(mud.albedo, vec3(0.33)));
    vec4 w = vec4(w_leaves, w_mudl, w_rock, w_mud);
    vec4 hw = w + hgt * 0.5;
    float mx = max(max(hw.x, hw.y), max(hw.z, hw.w));
    w = max(hw - (mx - 0.25), 0.0) * step(0.001, w);
    w /= max(w.x + w.y + w.z + w.w, 1e-4);

    vec3 albedo = leaves.albedo * w.x + mudl.albedo * w.y + rock.albedo * w.z + mud.albedo * w.w;
    vec3 arm = leaves.arm * w.x + mudl.arm * w.y + rock.arm * w.z + mud.arm * w.w;
    // Planar layers: tangent frame aligned with world X / -Z (glTF convention, green = up in texture).
    vec3 T = normalize(vec3(1, 0, 0) - Ng * Ng.x), B = normalize(cross(Ng, T));
    vec3 tn = normalize(leaves.n * w.x + mudl.n * w.y + mud.n * w.w + vec3(0, 0, 1) * w.z);
    vec3 N = normalize(T * tn.x + B * tn.y + Ng * tn.z);
    N = normalize(mix(N, rock_n, w.z));
    // Macro variation and wet darkening.
    albedo *= 0.82 + 0.36 * patchy;
    float rough = arm.g;
    albedo = mix(albedo, albedo * albedo * 0.9, wet * 0.6); // wet surfaces darken (porous)
    rough = mix(rough, 0.25, wet * 0.5);

    o_albedo = vec4(albedo, arm.r);
    o_normal = vec4(oct_encode01(N), clamp(rough, 0.05, 1.0), 2.0 / 3.0);
    o_material = vec4(0.0, 0.0, 0.0, wet);
    o_velocity = vec2(0.0);
}
