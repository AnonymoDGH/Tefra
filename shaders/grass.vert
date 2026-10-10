#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/grass.glsl"
// One invocation per vertex of a blade strip. gl_InstanceIndex = world cell of the grid, gl_VertexIndex = blade and
// triangle corner. Vertices that do not belong to a visible blade are sent outside the clip volume.
layout(location = 0) out vec3 v_pos;      // camera-relative world position
layout(location = 1) out vec3 v_normal;   // blade face normal, rounded towards up
layout(location = 2) out vec4 v_color_ao; // rgb albedo, a = ambient occlusion (darker at the root)

// Strip layout: quads between rows 0..seg-1 (two triangles each), then the tip triangle (row seg). Returns (row, side)
// where side 0 is the +s edge of the ribbon and side 1 the -s edge.
uvec2 grass_corner(uint seg, uint tri, uint k) {
    uint quads = 2u * (seg - 1u);
    if (tri < quads) {
        uint r = tri / 2u;
        if ((tri & 1u) == 0u)
            return k == 0u ? uvec2(r, 0u) : (k == 1u ? uvec2(r, 1u) : uvec2(r + 1u, 0u));
        return k == 0u ? uvec2(r, 1u) : (k == 1u ? uvec2(r + 1u, 1u) : uvec2(r + 1u, 0u));
    }
    return k == 0u ? uvec2(seg - 1u, 0u) : (k == 1u ? uvec2(seg - 1u, 1u) : uvec2(seg, 0u));
}

void main() {
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
    v_pos = vec3(0.0);
    v_normal = vec3(0.0, 1.0, 0.0);
    v_color_ao = vec4(0.0);

    const uint seg = uint(pc.p0.w);
    const uint per_blade = 3u * (2u * seg - 1u);
    const uint b = uint(gl_VertexIndex) / per_blade;
    const uint corner = uint(gl_VertexIndex) - b * per_blade;
    const uint tri = corner / 3u, k = corner - tri * 3u;
    const ivec2 cell = grass_cell(pc.cells.c[gl_InstanceIndex]);

    const float dist = grass_cell_dist(cell);
    Blade g;
    if (!grass_blade(cell, b, dist, g)) return;

    const uvec2 rs = grass_corner(seg, tri, k);
    const float t = float(rs.x) / float(seg); // 0 at the root, 1 at the tip
    const float sgn = rs.y == 0u ? 1.0 : -1.0;
    // Cubic Bezier spine in the blade's (d, up) plane.
    const vec3 D = vec3(g.d.x, 0.0, g.d.y), S3 = vec3(g.s.x, 0.0, g.s.y), Y = vec3(0.0, 1.0, 0.0);
    const vec3 P1 = D * (g.lean * 0.05) + Y * (g.height * 0.33);
    const vec3 P2 = D * (g.lean * 0.45) + Y * (g.height * 0.70);
    const vec3 P3 = D * g.lean + Y * g.height;
    const vec3 spine = grass_bez(vec3(0.0), P1, P2, P3, t);
    const vec3 T = normalize(grass_bez_d(vec3(0.0), P1, P2, P3, t));
    const float w = g.width * pow(1.0 - t, 0.8);
    const vec3 wind = grass_wind(g.root.xz, t);
    const vec3 world = g.root + spine + wind + S3 * (0.5 * w * sgn);

    const Frame f = pc.frame.f;
    const vec3 rel = world - f.camera_pos.xyz;
    // The +s edge comes first in every triangle, so cross(T, S3) is the front-facing (CCW) side (see grass.frag).
    const vec3 N = normalize(mix(normalize(cross(T, S3)), Y, 0.5));
    const vec3 base = mix(vec3(0.040, 0.105, 0.022), vec3(0.085, 0.150, 0.035), g.shade);
    const vec3 dry = vec3(0.34, 0.29, 0.11);
    const vec3 col = mix(base, dry, g.dry) * mix(0.85, 1.12, g.patchy) * mix(0.8, 1.05, t);

    v_pos = rel;
    v_normal = N;
    v_color_ao = vec4(col, mix(0.35, 1.0, t));
    gl_Position = f.view_proj * vec4(rel, 1.0);
}
