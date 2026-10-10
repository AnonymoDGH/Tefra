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

// Tile sizes (m): three planar layers and the triplanar rock. Parallax only within POM_NEAR metres.
const float TILE_LEAVES = 2.5, TILE_MUDL = 3.0, TILE_MUD = 3.5, TILE_ROCK = 6.0;
const float POM_NEAR = 16.0, POM_FADE = 9.0;

// Albedo alpha holds the layer height (Poly Haven Displacement, 1 = top); see terrain.cpp.
struct Layer { vec3 albedo; vec3 n; vec3 arm; float h; };

mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, s, -s, c); } // rot(a) * v rotates v by a
float hash1(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec3 hash3(vec2 p) {
    return fract(sin(vec3(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)), dot(p, vec2(419.2, 371.9)))) * 43758.5453);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash1(i), hash1(i + vec2(1, 0)), u.x), mix(hash1(i + vec2(0, 1)), hash1(i + vec2(1, 1)), u.x), u.y);
}

Layer sample_planar(uint a, uint n, uint r, vec2 uv) {
    Layer l;
    vec4 alb = texture(TEX2D(a, SMP_ANISO_REPEAT), uv);
    l.albedo = alb.rgb;
    l.h = alb.a;
    l.n = texture(TEX2D(n, SMP_ANISO_REPEAT), uv).xyz * 2.0 - 1.0;
    l.arm = texture(TEX2D(r, SMP_ANISO_REPEAT), uv).rgb;
    return l;
}
// M2 path (detail off): two scales blended by a macro noise value.
Layer sample_layer(uint a, uint n, uint r, vec2 world, float tile, float macro) {
    Layer l0 = sample_planar(a, n, r, world / tile);
    vec2 w2 = mat2(0.8, -0.6, 0.6, 0.8) * world / (tile * 3.7) + 0.37;
    Layer l1 = sample_planar(a, n, r, w2);
    float k = smoothstep(0.3, 0.7, macro);
    Layer l;
    l.albedo = mix(l0.albedo, l1.albedo, k * 0.6);
    l.n = normalize(mix(l0.n, l1.n, k * 0.6));
    l.arm = mix(l0.arm, l1.arm, k * 0.6);
    l.h = mix(l0.h, l1.h, k * 0.6);
    return l;
}
// Stochastic tiling after Heitz & Neyret 2018, simplified: a simplex grid gives three corners per point; each
// corner samples the layer with its own random rotation and offset, blended by the simplex kernel. Rotated
// derivatives keep mip selection valid. Albedo is variance-preserving around the texture mean (the 1x1 mip), which
// keeps the blend from going soft; the paper's histogram-preserving variant is not implemented.
Layer sample_stochastic(uint a, uint n, uint r, vec2 uv, vec2 dx, vec2 dy) {
    const float F2 = 0.36602540, G2 = 0.21132487;
    const vec2 p = uv * 0.35; // one grid cell is about 3 texture repeats
    vec2 s = floor(p + (p.x + p.y) * F2);
    vec2 x0 = p - (s - (s.x + s.y) * G2);
    vec2 o = x0.x > x0.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 x1 = x0 - o + G2;
    vec2 x2 = x0 - 1.0 + 2.0 * G2;
    vec3 k = max(vec3(0.5) - vec3(dot(x0, x0), dot(x1, x1), dot(x2, x2)), 0.0);
    k = k * k * k * k;
    k /= k.x + k.y + k.z;
    Layer l;
    l.albedo = vec3(0.0);
    l.n = vec3(0.0);
    l.arm = vec3(0.0);
    l.h = 0.0;
    const vec3 mu = textureLod(TEX2D(a, SMP_LINEAR_REPEAT), uv, 16.0).rgb; // texture mean
    for (int c = 0; c < 3; ++c) {
        vec2 cell = c == 0 ? s : (c == 1 ? s + o : s + 1.0);
        vec3 hs = hash3(cell);
        mat2 R = rot(hs.x * 6.2831853);
        vec2 q = R * uv + hs.yz * 16.0;
        vec2 qdx = R * dx, qdy = R * dy;
        vec4 alb = textureGrad(TEX2D(a, SMP_ANISO_REPEAT), q, qdx, qdy);
        vec3 nn = textureGrad(TEX2D(n, SMP_ANISO_REPEAT), q, qdx, qdy).xyz * 2.0 - 1.0;
        nn.xy = transpose(R) * nn.xy; // the pattern is rotated, so its tangent-space normals are too
        vec3 ar = textureGrad(TEX2D(r, SMP_ANISO_REPEAT), q, qdx, qdy).rgb;
        l.albedo += alb.rgb * k[c];
        l.n += nn * k[c];
        l.arm += ar * k[c];
        l.h += alb.a * k[c];
    }
    // Sum k = 1: the blend of three uncorrelated copies loses variance by sum(k^2); scale the deviation back.
    l.albedo = mu + (l.albedo - mu) / sqrt(dot(k, k));
    l.n = normalize(l.n);
    return l;
}

// Height seen by the parallax march (1 = top): the litter plane, or the dominant triplanar projection of the rock.
float pom_height(TerrainData t, vec3 p, float w_rock, int axis) {
    float h = textureLod(TEX2D(t.albedo0, SMP_ANISO_REPEAT), p.xz / TILE_LEAVES, 0.0).a;
    if (w_rock > 0.01) {
        vec2 ruv = axis == 0 ? p.zy : (axis == 1 ? p.xz : p.xy);
        h = mix(h, textureLod(TEX2D(t.albedo2, SMP_ANISO_REPEAT), ruv / TILE_ROCK, 0.0).a, w_rock);
    }
    return h;
}
// Parallax occlusion: linear search of the view ray through the height field (10 steps) with one linear
// refinement. Returns the world-space offset from the surface point to the visible point.
vec3 parallax_offset(TerrainData t, vec3 wp, vec3 V, vec3 Ng, float w_rock, int axis, float fade) {
    float cosv = max(dot(V, Ng), 0.25);
    float D = mix(0.04, 0.10, w_rock) * fade; // relief depth (m)
    const int STEPS = 10;
    float dz = D / float(STEPS);
    vec3 sv = -V * (dz / cosv);
    vec3 p = wp;
    float z = 0.0, z_prev = 0.0, d_prev = 0.0, z_hit = D;
    for (int i = 0; i < STEPS; ++i) {
        z += dz;
        p += sv;
        // d < 0: the ray is above the relief; d >= 0: it has gone below.
        float d = z - (1.0 - pom_height(t, p, w_rock, axis)) * D;
        if (d >= 0.0) {
            z_hit = mix(z_prev, z, d_prev / min(d_prev - d, -1e-6));
            break;
        }
        z_prev = z;
        d_prev = d;
    }
    return -V * (z_hit / cosv);
}

void main() {
    Frame f = pc.frame.f;
    TerrainData t = pc.terrain.t;
    const bool detail = (t.flags & 1u) != 0u;
    // Derivatives first, in uniform control flow.
    const vec2 dxw = dFdx(v_world), dyw = dFdy(v_world);

    float e = t.height.z;
    float hl = terrain_height(t, v_world - vec2(e, 0)), hr = terrain_height(t, v_world + vec2(e, 0));
    float hd = terrain_height(t, v_world - vec2(0, e)), hu = terrain_height(t, v_world + vec2(0, e));
    vec3 Ng = normalize(vec3(hl - hr, 2.0 * e, hd - hu));
    vec4 mask = texture(TEX2D(t.mask, SMP_LINEAR_CLAMP_MIP), terrain_uv(t, v_world));
    float wet = mask.r, patchy = mask.g;
    float slope = 1.0 - Ng.y;

    vec3 bw = pow(abs(Ng), vec3(4.0));
    bw /= bw.x + bw.y + bw.z;
    int axis = (bw.x > bw.y && bw.x > bw.z) ? 0 : (bw.y > bw.z ? 1 : 2);
    vec3 wp = v_pos + f.camera_pos.xyz;
    float w_rock = smoothstep(0.38, 0.58, slope + (patchy - 0.5) * 0.15);

    // Parallax occlusion near the camera; the offset moves every layer's UVs (the pass is cheap in the far field).
    vec3 off = vec3(0.0);
    float dist = length(v_pos);
    if (detail && dist < POM_NEAR)
        off = parallax_offset(t, wp, -v_pos / dist, Ng, w_rock, axis, 1.0 - smoothstep(POM_FADE, POM_NEAR, dist));
    const vec2 puv = v_world + off.xz;
    const vec3 wpo = wp + off;

    Layer leaves, mudl, mud;
    if (detail) {
        leaves = sample_stochastic(t.albedo0, t.normal0, t.arm0, puv / TILE_LEAVES, dxw / TILE_LEAVES, dyw / TILE_LEAVES);
        mudl = sample_stochastic(t.albedo1, t.normal1, t.arm1, puv / TILE_MUDL, dxw / TILE_MUDL, dyw / TILE_MUDL);
        mud = sample_stochastic(t.albedo3, t.normal3, t.arm3, puv / TILE_MUD, dxw / TILE_MUD, dyw / TILE_MUD);
    } else {
        leaves = sample_layer(t.albedo0, t.normal0, t.arm0, puv, TILE_LEAVES, patchy);
        mudl = sample_layer(t.albedo1, t.normal1, t.arm1, puv, TILE_MUDL, patchy);
        mud = sample_layer(t.albedo3, t.normal3, t.arm3, puv, TILE_MUD, patchy);
    }
    // Rock: triplanar.
    Layer rx = sample_planar(t.albedo2, t.normal2, t.arm2, wpo.zy / TILE_ROCK);
    Layer ry = sample_planar(t.albedo2, t.normal2, t.arm2, wpo.xz / TILE_ROCK);
    Layer rz = sample_planar(t.albedo2, t.normal2, t.arm2, wpo.xy / TILE_ROCK);
    Layer rock;
    rock.albedo = rx.albedo * bw.x + ry.albedo * bw.y + rz.albedo * bw.z;
    rock.arm = rx.arm * bw.x + ry.arm * bw.y + rz.arm * bw.z;
    rock.h = rx.h * bw.x + ry.h * bw.y + rz.h * bw.z;
    // Whiteout-style blend of the three tangent-space normals into world space.
    vec3 nx = vec3(0.0, rx.n.y, rx.n.x) * sign(Ng.x), ny = vec3(ry.n.x, 0.0, -ry.n.y), nz = vec3(rz.n.x, rz.n.y, 0.0) * sign(Ng.z);
    vec3 rock_n = normalize(Ng + (nx * bw.x + ny * bw.y + nz * bw.z));

    const bool coast = (t.flags & 2u) != 0u;
    float w_mud = wet * (1.0 - w_rock);
    float w_mudl = coast ? smoothstep(0.15, 0.85, mask.b + (patchy - 0.5) * 0.4) * (1.0 - wet) * (1.0 - w_rock)
                         : smoothstep(0.42, 0.68, patchy) * (1.0 - wet) * (1.0 - w_rock);
    float w_leaves = max(1.0 - w_rock - w_mud - w_mudl, 0.0);
    vec4 w = vec4(w_leaves, w_mudl, w_rock, w_mud);
    // Height-aware sharpening: the detail path uses the real layer heights (albedo alpha), the M2 path the
    // albedo luminance as a height proxy.
    vec4 hgt = detail ? vec4(leaves.h, mudl.h, rock.h, mud.h)
                      : vec4(dot(leaves.albedo, vec3(0.33)), dot(mudl.albedo, vec3(0.33)), dot(rock.albedo, vec3(0.33)), dot(mud.albedo, vec3(0.33)));
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
    if (detail && coast) {
        // Sand: bleached patches and darker heavy-mineral streaks at distance.
        float macro = 0.6 * vnoise(v_world / 30.0 + 3.1) + 0.4 * vnoise(v_world / 9.0 + 7.7);
        albedo *= mix(vec3(0.92, 0.9, 0.88), vec3(1.08, 1.06, 1.02), macro) * (1.0 - 0.15 * w.y * vnoise(v_world / 25.0));
        // Tropical hinterland: greener than the dry-grass scan.
        albedo *= mix(vec3(1.0), vec3(0.72, 1.0, 0.62), w.y);
        // The CC0 sand scans are warmer than bright quartz beach sand: pull the chroma back a little.
        float lum = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
        albedo = mix(vec3(lum), albedo, mix(0.78, 1.0, w.y + w.z)) * mix(1.12, 1.0, w.y + w.z);
    } else if (detail) {
        // Low-frequency macro variation at distance: moss-green patches on the ground, rust-red dry-needle patches.
        float macro = 0.65 * vnoise(v_world / 45.0 + 3.1) + 0.35 * vnoise(v_world / 13.0 + 7.7);
        float moss = smoothstep(0.55, 0.75, macro) * (1.0 - w.z);
        float dry = smoothstep(0.55, 0.75, vnoise(v_world / 70.0 + 9.0)) * w.x;
        albedo *= mix(vec3(1.0), vec3(0.82, 1.08, 0.66), moss * 0.6);
        albedo *= mix(vec3(1.0), vec3(1.22, 0.92, 0.70), dry * 0.7);
    }
    // Macro variation and wet darkening.
    albedo *= 0.82 + 0.36 * patchy;
    float rough = arm.g;
    if (coast) {
        // Wet sand: darker (water fills the pores) and glossy near the waterline.
        albedo *= mix(1.0, 0.6, wet);
        rough = mix(rough, 0.18, wet * 0.85);
    } else {
        albedo = mix(albedo, albedo * albedo * 0.9, wet * 0.6); // wet surfaces darken (porous)
        rough = mix(rough, 0.25, wet * 0.5);
    }

    o_albedo = vec4(albedo, arm.r);
    o_normal = vec4(oct_encode01(N), clamp(rough, 0.05, 1.0), 2.0 / 3.0);
    o_material = vec4(0.0, 0.0, 0.0, wet);
    o_velocity = vec2(0.0);
}
