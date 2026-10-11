#ifndef TEFRA_GRASS_GLSL
#define TEFRA_GRASS_GLSL
// Procedural grass (no vertex buffer). The CPU lists the 1 m world cells that can be visible (disc around the camera,
// frustum), and each cell holds pc.grid.w candidate blades. Blade data is a pure function of (cell, blade index), so
// blades stay put while the camera moves. Shared by grass.vert (drawing) and grass_count.comp (statistics).
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/common.glsl"
#include "common/terrain_data.glsl"

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CellListBuffer { uint c[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) buffer GrassCounter { uint v; };

layout(push_constant) uniform Push {
    FrameBuffer frame;      // 0
    TerrainBuffer terrain;  // 8
    CellListBuffer cells;   // 16: grid indices of the visible cells (near cells first)
    GrassCounter counter;   // 24: visible-blade counter (count pass only)
    vec4 p0;                // 32: x = disc radius (m), y = LOD start (m), z = tip wind amplitude (m), w = blade segments
    ivec4 grid;             // 48: x = world cell origin x, y = grid side (cells), z = world cell origin z, w = blades/cell
    uint cell_count;        // 64: entries in the cell list (count pass)
} pc;

// World cell of grid index gi (the grid is pc.grid.y x pc.grid.y cells, origin pc.grid.xz).
ivec2 grass_cell(uint gi) {
    uint side = uint(pc.grid.y);
    return ivec2(pc.grid.x, pc.grid.z) + ivec2(int(gi % side), int(gi / side));
}
// Horizontal distance (m) from the camera to the cell centre.
float grass_cell_dist(ivec2 cell) {
    return length(vec2(cell) + 0.5 - pc.frame.f.camera_pos.xz);
}

float grass_rnd(ivec2 cell, uint b, uint k) {
    uint h = hash_u32(uint(cell.x) ^ hash_u32(uint(cell.y) + 0x9E3779B9u * (b * 16u + k + 1u)));
    return float(h) * (1.0 / 4294967296.0);
}

vec3 grass_bez(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}
vec3 grass_bez_d(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1.0 - t;
    return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

struct Blade {
    vec3 root;    // world position (m)
    vec2 d;       // horizontal lean direction (unit)
    vec2 s;       // horizontal width direction (unit, perpendicular to d)
    float height; // m
    float width;  // m at the root
    float lean;   // m, horizontal offset of the tip
    float dry;    // 0 = green, 1 = dry yellow
    float shade;  // per-blade colour variation
    float patchy;  // ground patchiness (mask.g)
    float flower;  // > 0: a wildflower stem, the value picks the petal colour (meadow only)
    float phase;   // per-blade flutter phase
};

bool grass_meadow() { return (pc.terrain.t.flags & 4u) != 0u; }

// Smooth value noise for the wind field.
float grass_vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = float(hash_u32(uint(int(i.x)) * 73856093u ^ uint(int(i.y)) * 19349663u)) * (1.0 / 4294967296.0);
    float b = float(hash_u32(uint(int(i.x) + 1) * 73856093u ^ uint(int(i.y)) * 19349663u)) * (1.0 / 4294967296.0);
    float c = float(hash_u32(uint(int(i.x)) * 73856093u ^ uint(int(i.y) + 1) * 19349663u)) * (1.0 / 4294967296.0);
    float d = float(hash_u32(uint(int(i.x) + 1) * 73856093u ^ uint(int(i.y) + 1) * 19349663u)) * (1.0 / 4294967296.0);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Blade candidate (cell, b). Returns false when density removes it: wet ground, steep (rock) slopes, the edge of the
// disc and the LOD thinning in the far ring.
bool grass_blade(ivec2 cell, uint b, float dist, out Blade g) {
    TerrainData t = pc.terrain.t;
    vec2 root_xz = vec2(cell) + vec2(grass_rnd(cell, b, 0u), grass_rnd(cell, b, 1u));
    float e = t.height.z;
    float h0 = terrain_height(t, root_xz);
    float hx = terrain_height(t, root_xz + vec2(e, 0.0));
    float hz = terrain_height(t, root_xz + vec2(0.0, e));
    vec2 grad = vec2(hx - h0, hz - h0) / e;
    float ny = inversesqrt(1.0 + dot(grad, grad));
    vec4 mask = textureLod(TEX2D(t.mask, SMP_LINEAR_CLAMP), terrain_uv(t, root_xz), 0.0);
    float wet = mask.r, patchy = mask.g;
    float flat_k = smoothstep(0.60, 0.86, ny);
    // Shade-intolerant: under a closed conifer canopy the floor is needle litter, grass survives in the gaps.
    float shade = 1.0;
    if (pc.frame.f.canopy_map != 0xFFFFFFFFu) {
        vec2 cuv = (root_xz - pc.frame.f.canopy.xy) / pc.frame.f.canopy.z;
        float cover = textureLod(TEX2D(pc.frame.f.canopy_map, SMP_LINEAR_CLAMP), cuv, 0.0).r;
        shade = 1.0 - smoothstep(0.25, 0.75, cover) * 0.92;
    }
    const bool meadow = grass_meadow();
    float density = meadow ? (1.0 - 0.5 * wet) * mix(0.8, 1.0, patchy) * flat_k
                           : (1.0 - 0.9 * wet) * mix(0.5, 1.0, patchy) * flat_k * shade;
    float far_k = smoothstep(pc.p0.y, pc.p0.x, dist);
    float lod = meadow ? mix(1.0, 0.3, far_k) * (1.0 - 0.7 * smoothstep(0.75, 1.0, dist / pc.p0.x))
                       : mix(1.0, 0.75, far_k);
    float fade = 1.0 - smoothstep(pc.p0.x - 4.0, pc.p0.x, dist);
    if (grass_rnd(cell, b, 2u) >= density * lod * fade) return false;

    float a = grass_rnd(cell, b, 3u) * 6.2831853;
    g.d = vec2(cos(a), sin(a));
    g.s = vec2(-g.d.y, g.d.x);
    g.root = vec3(root_xz.x, h0 - 0.02, root_xz.y);
    g.height = mix(0.18, 0.62, grass_rnd(cell, b, 4u)) * mix(0.85, 1.15, patchy);
    g.width = mix(0.025, 0.045, grass_rnd(cell, b, 5u));
    g.lean = g.height * mix(0.05, 0.35, grass_rnd(cell, b, 6u));
    float r = grass_rnd(cell, b, 7u);
    g.dry = clamp(r * 0.9 + 0.2 * (1.0 - wet) - 0.25, 0.0, 1.0);
    g.shade = grass_rnd(cell, b, 8u);
    g.patchy = patchy;
    g.flower = 0.0;
    g.phase = grass_rnd(cell, b, 9u) * 6.2831853;
    if (meadow) {
        // Tall meadow grass in clumps, sun-dried swathes at the 20 m scale, scattered wildflowers.
        float clump = grass_vnoise(root_xz / 3.0);
        g.height = mix(0.30, 0.95, pow(grass_rnd(cell, b, 4u), 0.8)) * mix(0.7, 1.25, clump);
        g.width = mix(0.018, 0.034, grass_rnd(cell, b, 5u));
        g.lean = g.height * mix(0.08, 0.30, grass_rnd(cell, b, 6u));
        float swathe = smoothstep(0.55, 0.85, grass_vnoise(root_xz / 22.0 + 7.0));
        g.dry = clamp(r * 0.5 - 0.2 + swathe * 0.7, 0.0, 1.0);
        if (grass_rnd(cell, b, 10u) < 0.018 * smoothstep(0.3, 0.6, grass_vnoise(root_xz / 9.0 + 3.0))) {
            g.flower = 0.01 + grass_rnd(cell, b, 11u);
            g.height *= 0.75;
            g.width = 0.012;
        }
        // Far ring: fewer but wider blades keep the coverage, and they shrink into the ground towards the edge of
        // the disc so the field melts into the grass-coloured terrain instead of ending in a line.
        g.width *= mix(1.0, 2.4, far_k);
        g.height *= 1.0 - 0.85 * smoothstep(0.45, 1.0, dist / pc.p0.x);
    }
    return true;
}

// Wind strength (0..~1.3) at w: gusts travel across the valley.
float grass_gust(vec2 w) {
    float time = pc.frame.f.camera_pos.w;
    vec2 dir = vec2(0.86, 0.51);
    if (grass_meadow()) {
        // Gust fronts: a noise field advected downwind (~4 m/s) so bright "waves" roll across the meadow.
        vec2 p = w - dir * (time * 4.0);
        float n = 0.65 * grass_vnoise(p / 16.0) + 0.35 * grass_vnoise(p / 5.5 + 11.0);
        return 0.25 + 1.05 * smoothstep(0.30, 0.85, n);
    }
    return 0.6 + 0.4 * sin(dot(w, dir) * 0.09 - time * 1.1) + 0.15 * sin(dot(w, vec2(-0.4, 0.7)) * 0.3 + time * 2.3);
}

// Tip displacement from wind at blade parameter t (0 root, 1 tip): the steady lean of the gust plus a faster flutter.
// Meadow blades bend in proportion to their height and keep their length (the tip drops as they lean).
vec3 grass_wind(vec2 w, float t, float height, float phase) {
    float time = pc.frame.f.camera_pos.w;
    vec2 dir = vec2(0.86, 0.51);
    float gust = grass_gust(w);
    if (grass_meadow()) {
        float flutter = sin(time * 6.5 + phase + dot(w, dir) * 1.7) * (0.08 + 0.10 * gust);
        float bend = height * (0.42 * gust + flutter) * t * t;
        vec2 side = vec2(-dir.y, dir.x) * sin(time * 3.1 + phase * 1.3) * 0.05 * height * t;
        float drop = -0.5 * bend * bend / max(height, 0.05);
        return vec3(dir.x * bend + side.x, drop, dir.y * bend + side.y);
    }
    return vec3(dir.x, 0.0, dir.y) * (pc.p0.z * gust * t * t);
}
#endif
