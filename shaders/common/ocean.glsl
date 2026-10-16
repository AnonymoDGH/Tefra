#ifndef TEFRA_OCEAN_GLSL
#define TEFRA_OCEAN_GLSL
// Ocean surface (ocean.h): open-sea Gerstner waves + shore-driven surf (shoaling, breaking bores, swash).
// Shared by ocean.vert (displacement) and ocean.frag (normals, foam). World units: metres, seconds.
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
#include "common/terrain_data.glsl"

struct OceanWave { vec4 dir_k; vec4 amp; }; // see OceanWaveGPU
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer OceanWaves { OceanWave w[]; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer OceanVerts { vec4 v[]; };

layout(push_constant) uniform Push {
    FrameBuffer frame;     // 0
    OceanVerts verts;      // 8
    OceanWaves waves;      // 16
    TerrainBuffer terrain; // 24 (valid when has_terrain != 0)
    uint hdr;              // 32: lit HDR (refraction source)
    uint depth;            // 36
    uint has_terrain;      // 40
    float time;            // 44
    vec4 params;           // 48: x = still level (world y), y = surf height (m), z = camera world x, w = camera world z
} pc;

const uint OCEAN_GEOMETRY_WAVES = 16u; // Ocean::kGeometryWaves
const uint OCEAN_DETAIL_WAVES = 14u;
const float OCEAN_SHORE_RANGE = 2048.0; // Terrain::kShoreRange

float ocean_hash(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float ocean_vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(ocean_hash(i), ocean_hash(i + vec2(1, 0)), u.x),
               mix(ocean_hash(i + vec2(0, 1)), ocean_hash(i + vec2(1, 1)), u.x), u.y);
}

// Value noise with analytic derivatives: returns (value, d/dx, d/dy). Non-periodic micro detail for the normals.
vec3 ocean_noised(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float a = ocean_hash(i), b = ocean_hash(i + vec2(1, 0)), c = ocean_hash(i + vec2(0, 1)), d = ocean_hash(i + vec2(1, 1));
    float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y, du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

// Water depth below the still level and the shore distance field (> 0 at sea) with its downhill direction.
struct Shore { float depth; float dist; vec2 to_shore; };
float ocean_shore_dist(TerrainData t, vec2 xz) {
    return (textureLod(TEX2D(t.shore, SMP_LINEAR_CLAMP), terrain_uv(t, xz), 0.0).r - 0.5) * OCEAN_SHORE_RANGE;
}
Shore ocean_shore(vec2 xz) {
    Shore s;
    s.depth = 80.0;
    s.dist = 1e4;
    s.to_shore = vec2(0.0, 1.0);
    if (pc.has_terrain == 0u) return s;
    TerrainData t = pc.terrain.t;
    vec2 uv = terrain_uv(t, xz);
    // Past the map edge the sea floor keeps falling away: blend to open water over 200 m.
    float outside = max(max(-uv.x, uv.x - 1.0), max(-uv.y, uv.y - 1.0)) * t.extent.z;
    float edge = smoothstep(0.0, 200.0, outside);
    s.depth = mix(pc.params.x - terrain_height(t, xz), 80.0, edge);
    if (t.shore == 0u) return s;
    const float e = 6.0;
    float d0 = ocean_shore_dist(t, xz);
    vec2 g = vec2(ocean_shore_dist(t, xz + vec2(e, 0)) - ocean_shore_dist(t, xz - vec2(e, 0)),
                  ocean_shore_dist(t, xz + vec2(0, e)) - ocean_shore_dist(t, xz - vec2(0, e)));
    s.dist = mix(d0, 1e4, edge);
    s.to_shore = dot(g, g) > 1e-6 ? -normalize(g) : vec2(0.0, 1.0);
    return s;
}

struct OceanSample {
    vec3 disp;     // displacement from the still point (m)
    vec2 slope;    // dY/dx, dY/dz of the surface (for the normal)
    float jacobian; // < ~0.3: folding crests (whitecaps)
    float break_foam; // 0..1: breaking crest and the foam behind it
    float surf;    // 0..1: surf-zone weight (for the foam lace and the water tint)
    float lost_var; // slope variance of the waves filtered out by the pixel footprint (-> roughness)
};

// Fraction of a wave of wavenumber k kept at a pixel footprint (m): waves under ~2.5 pixels are filtered out.
float ocean_keep(float k, float footprint) { return 1.0 - smoothstep(0.25, 0.6, footprint * k / 3.14159265); }

// Bore profile of a broken wave: fast rise over the front, long linear fall behind it. x = phase / 2pi.
// Returns value (-1..1) and derivative with respect to the phase (per radian).
vec2 ocean_bore(float x) {
    const float front = 0.14;
    float u = fract(x);
    if (u < front) {
        float s = u / front;
        float v = s * s * (3.0 - 2.0 * s);
        return vec2(-1.0 + 2.0 * v, 2.0 * 6.0 * s * (1.0 - s) / front / 6.2831853);
    }
    return vec2(1.0 - 2.0 * (u - front) / (1.0 - front), -2.0 / (1.0 - front) / 6.2831853);
}

OceanSample ocean_eval(vec2 xz, float t, Shore sh, bool want_foam, float footprint) {
    OceanSample o;
    o.disp = vec3(0.0);
    o.slope = vec2(0.0);
    o.lost_var = 0.0;
    float jxx = 1.0, jzz = 1.0, jxz = 0.0;
    // Open sea: the swell fades over the shelf (refraction and breaking hand it to the surf system).
    float deep = smoothstep(0.5, 12.0, sh.depth) * 0.8 + 0.2 * smoothstep(-0.3, 1.0, sh.depth);
    for (uint i = 0u; i < OCEAN_GEOMETRY_WAVES; ++i) {
        OceanWave w = pc.waves.w[i];
        vec2 D = w.dir_k.xy;
        float k = w.dir_k.z, Q = w.amp.y;
        float keep = ocean_keep(k, footprint);
        float A = w.amp.x * deep;
        o.lost_var += 0.5 * (k * A) * (k * A) * (1.0 - keep);
        A *= keep; // filtered by the grid cell (vertex) or the pixel (fragment)
        // Filtered out completely (keep == 0, typical for distant pixels): every term below is exactly zero.
        if (keep <= 0.0) continue;
        float th = k * dot(D, xz) - w.dir_k.w * t + w.amp.z;
        float c = cos(th), s = sin(th);
        o.disp += vec3(Q * A * D.x * c, A * s, Q * A * D.y * c);
        o.slope += k * A * c * D;
        float qs = Q * k * A * s;
        jxx -= qs * D.x * D.x;
        jzz -= qs * D.y * D.y;
        jxz -= qs * D.x * D.y;
    }
    o.jacobian = jxx * jzz - jxz * jxz;

    // Surf: crests run up the shore-distance field. Shallow-water speed ~ 3 m/s, 8 s period.
    o.break_foam = 0.0;
    o.surf = 0.0;
    float H = pc.params.y;
    if (H > 0.0 && sh.dist < 400.0) {
        float fade_in = 1.0 - smoothstep(4.0, 13.0, sh.depth);
        float shoal = 1.0 + 0.45 * (1.0 - smoothstep(1.3, 5.0, sh.depth));
        float broken = smoothstep(-0.45, 1.5, sh.depth);
        float alive = smoothstep(-0.9, -0.25, sh.depth); // swash limit up the beach
        // Wave sets: groups of bigger waves drifting along the shore.
        float sets = 0.45 + 0.55 * ocean_vnoise(xz / 85.0 + vec2(t * 0.013, -t * 0.02));
        float A = 0.5 * H * fade_in * shoal * mix(0.3, 1.0, broken) * alive * sets;
        o.surf = fade_in * alive;
        const float k = 6.2831853 / 24.0, omega = 6.2831853 / 8.0;
        // Small along-shore phase drift so crests are not perfect contour lines.
        float along = ocean_vnoise(xz / 140.0) * 2.5;
        float th = k * sh.dist + omega * t + along;
        float bore = 1.0 - smoothstep(0.7, 3.0, sh.depth);
        vec2 b = ocean_bore((th - 1.5707963) / 6.2831853);
        float y = mix(sin(th), b.x, bore);
        float dy = mix(cos(th), b.y, bore); // d(profile)/d(theta)
        o.disp.y += A * y;
        // d(theta)/d(xz) = k * grad(dist) = -k * to_shore
        o.slope += A * dy * (-k) * sh.to_shore * ocean_keep(k * 3.0, footprint);
        float Q = mix(0.25, 0.8, 1.0 - smoothstep(1.2, 5.0, sh.depth)) * (1.0 - 0.7 * bore);
        o.disp.xz += sh.to_shore * (Q * A * cos(th));
        if (want_foam) {
            // Foam on the bore front and a trail behind it, only where the wave has broken.
            float zone = (1.0 - smoothstep(0.8, 2.6, sh.depth)) * alive * smoothstep(0.08, 0.25, A);
            // u grows seaward: the crest is at u ~ 0.14 (end of the rise), foam trails behind it.
            float u = fract((th - 1.5707963) / 6.2831853);
            float uu = max(u - 0.11, 0.0);
            float front = smoothstep(0.05, 0.13, u) * (exp(-uu * 7.0) + 0.35 * exp(-uu * 2.2));
            o.break_foam = clamp(zone * front * 1.3, 0.0, 1.0);
        }
    }
    return o;
}
#endif
