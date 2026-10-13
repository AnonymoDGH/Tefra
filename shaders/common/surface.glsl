// Procedural surfaces for Part materials (material flag bit 4, Material enum index in pad1). World-space solid or
// planar patterns, so neighbouring parts line up and nothing needs UVs or textures. Every high-frequency term fades
// out by its screen footprint (fwidth) to avoid aliasing; relief is applied as a height bump with the surface
// gradient of Mikkelsen 2010 ("Bump mapping unparametrized surfaces on the GPU").
// Needs hash13 / value_noise / fbm3 from the including shader.

const uint kFlagSurface = 16u;

struct Surface {
    vec3 tint;    // multiplies the part colour
    float height; // metres-ish relief, only its derivatives matter
    float rough;  // multiplies the material roughness
    float ao;
};

vec2 hash22(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz) * q.zy);
}

// Fade of a pattern of `freq` cycles per metre at this pixel: 1 when well resolved, 0 below Nyquist.
float band(float freq, float footprint) { return 1.0 - smoothstep(0.3, 0.8, freq * footprint); }

// 2D Voronoi: x = distance to the nearest cell centre, y = to the second, z = cell id hash.
vec3 voronoi(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float d1 = 8.0, d2 = 8.0, id = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(x, y), o = hash22(i + g) * 0.8 + 0.1;
            float d = length(g + o - f);
            if (d < d1) {
                d2 = d1;
                d1 = d;
                id = hash13(vec3(i + g, 7.0));
            } else if (d < d2) {
                d2 = d;
            }
        }
    return vec3(d1, d2, id);
}

// Planar coordinates on the plane facing the dominant axis of the normal (u along the longer world axis there).
vec2 planar(vec3 P, vec3 N) {
    vec3 a = abs(N);
    if (a.y >= a.x && a.y >= a.z) return P.xz;
    return a.x >= a.z ? vec2(P.z, P.y) : vec2(P.x, P.y);
}

Surface surface_pattern(uint kind, vec3 P, vec3 N, float fp) {
    Surface s;
    s.tint = vec3(1.0);
    s.height = 0.0;
    s.rough = 1.0;
    s.ao = 1.0;
    vec2 uv = planar(P, N);
    bool top = N.y > 0.7;
    if (kind == 0u) { // Plastic: faint mottling
        s.tint *= 0.97 + 0.06 * value_noise(P * 3.0);
        s.height = 0.0004 * value_noise(P * 60.0) * band(60.0, fp);
    } else if (kind == 2u) { // Wood: grain stripes along the dominant planar u, rings warped by noise
        float w = fbm3(vec3(uv.x * 0.6, uv.y * 6.0, 0.0));
        float rings = fract(uv.y * 9.0 + w * 3.0);
        float grain = smoothstep(0.0, 0.25, rings) * (1.0 - smoothstep(0.55, 1.0, rings));
        float fine = value_noise(vec3(uv.x * 2.0, uv.y * 120.0, 3.0)) * band(120.0, fp);
        s.tint *= mix(0.72, 1.08, grain) * (0.92 + 0.16 * fine) * (0.9 + 0.2 * value_noise(P * 0.7));
        s.height = (grain * 0.0015 + fine * 0.0006) * band(9.0, fp);
    } else if (kind == 3u) { // WoodPlanks: 0.25 m boards with per-board tone and dark gaps
        float bw = 0.25;
        float row = floor(uv.y / bw);
        float along = uv.x + hash13(vec3(row, 1.0, 2.0)) * 3.0;
        float board = floor(along / 2.4);
        float id = hash13(vec3(row, board, 5.0));
        vec2 f = vec2(fract(along / 2.4) * 2.4, fract(uv.y / bw) * bw);
        float gap = min(min(f.y, bw - f.y), min(f.x, 2.4 - f.x));
        float seam = smoothstep(0.003, 0.009, gap);
        float g = fbm3(vec3(along * 0.8, uv.y * 30.0, id * 17.0));
        float fine = value_noise(vec3(along * 3.0, uv.y * 140.0, id)) * band(140.0, fp);
        s.tint *= mix(0.78, 1.12, id) * mix(0.82, 1.08, g) * (0.94 + 0.12 * fine);
        s.tint *= mix(0.25, 1.0, mix(1.0, seam, band(60.0, fp)));
        s.height = (seam * 0.004 + g * 0.0008) * band(40.0, fp);
        s.ao = mix(0.6, 1.0, seam);
    } else if (kind == 4u) { // Metal: brushed streaks
        float streak = value_noise(vec3(uv.x * 2.0, uv.y * 300.0, 1.0)) * band(300.0, fp);
        float blotch = fbm3(P * 1.5);
        s.tint *= 0.92 + 0.10 * streak + 0.06 * blotch;
        s.rough = mix(0.75, 1.25, streak * 0.6 + blotch * 0.4);
        s.height = 0.0003 * streak;
    } else if (kind == 5u) { // DiamondPlate: raised diamonds on a 0.1 m grid
        vec2 g = uv * 10.0;
        vec2 c = fract(g) - 0.5;
        bool odd = mod(floor(g.x) + floor(g.y), 2.0) > 0.5;
        vec2 r = odd ? vec2(c.x + c.y, c.x - c.y) : vec2(c.x - c.y, c.x + c.y);
        float d = max(abs(r.x) * 3.0, abs(r.y) * 0.9);
        float bump = 1.0 - smoothstep(0.25, 0.35, d);
        s.height = bump * 0.003 * band(10.0, fp);
        s.rough = mix(1.0, 0.7, bump);
        s.tint *= 0.95 + 0.08 * value_noise(P * 4.0);
    } else if (kind == 6u) { // Concrete: blotches, speckle, pores
        float blot = fbm3(P * 0.8);
        float speck = hash13(floor(P * 200.0)) * band(200.0, fp);
        float pores = smoothstep(0.75, 0.9, value_noise(P * 45.0)) * band(45.0, fp);
        s.tint *= (0.86 + 0.2 * blot) * (0.95 + 0.1 * speck) * (1.0 - 0.25 * pores);
        s.height = -pores * 0.0015 + value_noise(P * 25.0) * 0.0006 * band(25.0, fp);
        s.ao = 1.0 - 0.3 * pores;
    } else if (kind == 7u) { // Brick: running bond 0.6 x 0.25 m with recessed mortar
        vec2 b = vec2(uv.x / 0.6, uv.y / 0.25);
        float row = floor(b.y);
        b.x += mod(row, 2.0) * 0.5;
        vec2 cell = floor(b), f = fract(b) * vec2(0.6, 0.25);
        float edge = min(min(f.x, 0.6 - f.x), min(f.y, 0.25 - f.y));
        float brick = smoothstep(0.008, 0.02, edge);
        float id = hash13(vec3(cell, 3.0));
        float rough_face = fbm3(P * 18.0) * band(18.0, fp);
        vec3 face = mix(vec3(0.82), vec3(1.15), id) * (0.88 + 0.22 * rough_face);
        vec3 mortar = vec3(1.9, 1.85, 1.75) * (0.85 + 0.2 * value_noise(P * 30.0));
        float bb = band(25.0, fp); // mortar lines are ~4 cm wide
        s.tint *= mix(vec3(1.0), mix(mortar, face, brick), bb);
        s.height = (brick * 0.012 + rough_face * 0.0015) * bb;
        s.ao = mix(1.0, mix(0.55, 1.0, brick), bb);
        s.rough = mix(1.1, 0.95, brick);
    } else if (kind == 8u) { // Cobblestone: rounded Voronoi stones in dark joints
        vec3 v = voronoi(uv * 1.8);
        float joint = v.y - v.x;
        float stone = smoothstep(0.03, 0.18, joint);
        float bb = band(12.0, fp);
        float detail = fbm3(P * 14.0) * band(14.0, fp);
        s.tint *= mix(vec3(1.0), mix(vec3(0.35), vec3(0.8 + 0.4 * v.z) * (0.85 + 0.3 * detail), stone), bb);
        s.height = (sqrt(stone) * 0.03 + detail * 0.002) * bb;
        s.ao = mix(1.0, mix(0.45, 1.0, stone), bb);
    } else if (kind == 9u) { // Granite: speckled crystals
        vec3 cr = voronoi(uv * 45.0);
        float bg = band(45.0, fp);
        float dark = step(0.78, cr.z) * bg, light = step(cr.z, 0.18) * bg;
        float pink = step(0.45, cr.z) * step(cr.z, 0.6) * bg;
        float blot = fbm3(P * 2.0);
        s.tint *= (0.85 + 0.25 * blot) * (1.0 - 0.6 * dark + 0.45 * light) * mix(vec3(1.0), vec3(1.12, 0.98, 0.92), pink);
        s.height = value_noise(P * 30.0) * 0.0008 * band(30.0, fp);
        s.rough = 0.9 + 0.2 * dark;
    } else if (kind == 10u) { // Marble: warped veins
        float w = fbm3(P * 0.9) * 6.0;
        float vein = pow(1.0 - abs(sin((P.x + P.y * 0.6 + P.z * 0.3) * 2.2 + w)), 12.0);
        float soft = fbm3(P * 3.0);
        s.tint *= (0.94 + 0.1 * soft) * (1.0 - 0.55 * vein);
        s.rough = 1.0 + vein * 0.8;
    } else if (kind == 11u) { // Slate: layered strata with chipped edges
        float layer = P.y * 5.0 + fbm3(P * vec3(1.5, 0.3, 1.5)) * 2.0;
        float strata = fract(layer);
        float bb = band(5.0, fp);
        float chips = fbm3(P * 9.0);
        s.tint *= (0.75 + 0.4 * chips) * mix(1.0, 0.8 + 0.35 * hash13(vec3(floor(layer), 1.0, 1.0)), bb);
        s.height = (top ? chips * 0.004 : (smoothstep(0.0, 0.15, strata) * 0.004 + chips * 0.002)) * bb;
    } else if (kind == 12u) { // Sand: grains plus wind ripples on top faces
        float grain = hash13(floor(P * 300.0)) * band(300.0, fp);
        float speck = step(0.93, hash13(floor(P * 70.0) + 3.0)) * band(70.0, fp);
        float ripple = top ? sin(P.x * 5.0 + P.z * 2.0 + fbm3(P * 0.6) * 6.0) * band(1.6, fp) : 0.0;
        float blot = fbm3(P * 0.5);
        s.tint *= (0.85 + 0.25 * blot) * (0.92 + 0.16 * grain) * (1.0 - 0.3 * speck) * (1.0 + 0.06 * ripple);
        s.height = ripple * 0.025 + grain * 0.0005;
    } else if (kind == 13u) { // Grass: tufts with dark gaps, blade streaks, dry patches and soil
        float patch_ = fbm3(P * 0.3);
        float mid = fbm3(P * 2.0);
        float bt = band(6.0, fp);
        float clump = fbm3(vec3(uv * 5.0, 2.0));
        float gaps = mix(1.0, 0.72 + 0.4 * clump, bt);
        vec3 q1 = top ? vec3(P.x * 22.0, P.z * 22.0, 0.0) : vec3(P.x * 22.0 + P.z * 22.0, P.y * 5.0, 0.0);
        vec3 q2 = top ? vec3(P.x * 75.0, P.z * 75.0, 5.0) : vec3(P.x * 75.0 + P.z * 75.0, P.y * 14.0, 5.0);
        float b1 = value_noise(q1) * band(22.0, fp), b2 = value_noise(q2) * band(75.0, fp);
        float blades = 0.6 * b1 + 0.4 * b2;
        float dry = smoothstep(0.55, 0.8, patch_ + 0.1 * (clump - 0.5) * bt);
        float soil = smoothstep(0.8, 0.95, mid) * 0.7;
        vec3 t = mix(vec3(0.85, 1.0, 0.8), vec3(1.3, 1.15, 0.6), dry) * mix(0.9, 1.1, clump * bt + 0.5 * (1.0 - bt));
        t *= 0.75 + 0.5 * mid;
        t = mix(t, vec3(0.55, 0.42, 0.30) * 1.3, soil);
        t *= (0.6 + 0.65 * blades) * gaps;
        s.tint *= t;
        s.height = blades * 0.008 + (gaps - 1.0) * 0.015 + mid * 0.004;
        s.ao = mix(1.0, gaps, 0.6) * (0.8 + 0.2 * blades);
        s.rough = 1.0;
    } else if (kind == 14u) { // Ground: soil with pebbles
        float blot = fbm3(P * 0.9);
        float damp = smoothstep(0.5, 0.75, fbm3(P * 0.35 + 9.0));
        vec3 v = voronoi(uv * 9.0);
        float pebble = (1.0 - smoothstep(0.12, 0.28, v.x)) * step(0.72, v.z) * band(9.0, fp);
        float crumbs = value_noise(P * 40.0) * band(40.0, fp);
        s.tint *= (0.65 + 0.6 * blot) * (0.8 + 0.4 * crumbs) * (1.0 + 0.6 * pebble) * (1.0 - 0.35 * damp);
        s.rough = 1.0 - 0.3 * damp;
        s.height = pebble * 0.01 + crumbs * 0.002 + blot * 0.004;
    } else if (kind == 15u) { // Rock: ridged cracks and lichen-free mottling
        float r = 1.0 - abs(value_noise(P * 3.0) * 2.0 - 1.0);
        float cracks = smoothstep(0.92, 0.99, r) * band(3.0, fp);
        float big = fbm3(P * 0.8), det = fbm3(P * 7.0) * band(7.0, fp);
        s.tint *= (0.75 + 0.4 * big) * (0.9 + 0.2 * det) * (1.0 - 0.5 * cracks);
        s.height = big * 0.05 + det * 0.006 - cracks * 0.01;
        s.ao = 1.0 - 0.4 * cracks;
    } else if (kind == 16u) { // Ice: clear with frosty blotches and fine cracks
        vec3 v = voronoi(uv * 1.6);
        float crack = (1.0 - smoothstep(0.0, 0.03, v.y - v.x)) * band(1.6, fp);
        float frost = fbm3(P * 2.0);
        s.tint *= vec3(0.94, 0.98, 1.04) * (1.0 + 0.4 * crack + 0.15 * frost);
        s.rough = 1.0 + 4.0 * frost * frost + 3.0 * crack;
        s.height = -crack * 0.002;
    } else if (kind == 19u) { // Fabric: plain weave
        vec2 w = uv * 180.0;
        float bb = band(180.0, fp);
        float weave = (sin(w.x * 6.2832) * 0.5 + 0.5) * (sin(w.y * 6.2832) * 0.5 + 0.5);
        float over = step(0.5, fract((floor(w.x) + floor(w.y)) * 0.5));
        s.tint *= mix(1.0, 0.85 + 0.25 * mix(weave, 1.0 - weave, over), bb) * (0.95 + 0.1 * value_noise(P * 5.0));
        s.height = weave * 0.0004 * bb;
    } else if (kind == 20u) { // Foil: crinkles
        float c = 1.0 - abs(value_noise(P * 12.0) * 2.0 - 1.0);
        s.height = c * 0.004 * band(12.0, fp);
        s.rough = 0.8 + 0.6 * c;
    } else if (kind == 21u) { // Wallpaper: 53 cm rolls, thin stripes and a faint diamond motif, aged and stained
        float u = top ? uv.x : uv.x, v = top ? uv.y : P.y;
        float bs = band(30.0, fp), bm = band(9.0, fp);
        float stripe = 1.0 - smoothstep(0.0, 0.08, abs(fract(u / 0.105) - 0.5) - 0.42);
        vec2 c = vec2(fract(u / 0.105) - 0.5, fract(v / 0.16) - 0.5);
        float diamond = 1.0 - smoothstep(0.02, 0.06, abs(abs(c.x) * 1.5 + abs(c.y) - 0.32));
        float seam = 1.0 - smoothstep(0.0, 0.002, abs(fract(u / 0.53) - 0.5) - 0.497);
        float paper = value_noise(P * 90.0) * band(90.0, fp);
        float blot = smoothstep(0.52, 0.8, fbm3(P * vec3(0.5, 0.35, 0.5) + 3.0));
        float drip = smoothstep(0.6, 0.9, value_noise(vec3(u * 9.0, v * 0.8, 1.0))) * smoothstep(0.62, 0.3, fract(v * 0.37));
        vec3 t = vec3(1.0);
        t *= mix(1.0, 0.88, stripe * bs * 0.8) * mix(1.0, 0.9, diamond * bm * 0.7);
        t *= mix(1.0, 0.8, seam * bs) * (0.96 + 0.08 * paper);
        t *= mix(vec3(1.0), vec3(0.78, 0.7, 0.52), blot * 0.6) * mix(vec3(1.0), vec3(0.82, 0.76, 0.6), drip * 0.5);
        s.tint *= t * (0.94 + 0.1 * fbm3(P * 1.3));
        s.height = (paper * 0.0004 - seam * 0.0008) + stripe * 0.0002 * bs;
        s.rough = 0.95;
    } else if (kind == 22u) { // Carpet: dense fibre noise, worn paths and damp patches
        float bf = band(160.0, fp);
        float fib = hash13(floor(P * 320.0)) * band(320.0, fp);
        float tuft = value_noise(P * 160.0) * bf;
        float mott = fbm3(P * 1.6);
        float damp = smoothstep(0.55, 0.78, fbm3(P * 0.45 + 7.0));
        s.tint *= (0.82 + 0.3 * mott) * (0.86 + 0.22 * tuft) * (0.92 + 0.12 * fib);
        s.tint *= mix(vec3(1.0), vec3(0.62, 0.58, 0.48), damp * 0.7);
        s.height = tuft * 0.002 + fib * 0.0006;
        s.rough = mix(1.0, 0.75, damp); // wet carpet catches a dull sheen
        s.ao = 0.85 + 0.15 * tuft;
    } else if (kind == 23u) { // CeilingTile: 61 x 122 cm acoustic tiles in a white T-bar grid
        vec2 g = vec2(uv.x / 0.61, uv.y / 1.22);
        vec2 f = fract(g) * vec2(0.61, 1.22);
        float edge = min(min(f.x, 0.61 - f.x), min(f.y, 1.22 - f.y));
        float tbar = 1.0 - smoothstep(0.012, 0.02, edge);
        float id = hash13(vec3(floor(g), 9.0));
        float pores = smoothstep(0.7, 0.85, value_noise(P * 140.0)) * band(140.0, fp);
        float fissure = smoothstep(0.8, 0.95, value_noise(vec3(P.x * 30.0, P.z * 8.0, 3.0))) * band(30.0, fp) * 0.5;
        float stain = step(0.86, id) * smoothstep(0.3, 0.7, fbm3(P * 2.2 + id * 13.0));
        float bb = band(18.0, fp);
        vec3 tile = vec3(0.97 + 0.04 * id) * (1.0 - 0.3 * pores - 0.2 * fissure);
        tile *= mix(vec3(1.0), vec3(0.75, 0.62, 0.42), stain * 0.8);
        s.tint *= mix(tile, vec3(1.05), tbar * bb);
        s.height = (1.0 - tbar) * 0.006 * bb - pores * 0.0008 - fissure * 0.0006;
        s.rough = mix(1.0, 0.45, tbar * bb);
        s.ao = mix(1.0, 0.8, (1.0 - smoothstep(0.02, 0.04, edge)) * (1.0 - tbar) * bb);
    }
    return s;
}

// Perturbs N by the screen-space gradient of h (world position P).
vec3 bump_from_height(vec3 N, vec3 P, float h, float strength) {
    vec3 dpdx = dFdx(P), dpdy = dFdy(P);
    float dhx = dFdx(h), dhy = dFdy(h);
    vec3 r1 = cross(dpdy, N), r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    if (abs(det) < 1e-12) return N;
    vec3 grad = sign(det) * (dhx * r1 + dhy * r2);
    return normalize(abs(det) * N - grad * strength);
}
