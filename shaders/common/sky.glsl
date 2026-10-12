#ifndef TEFRA_SKY_GLSL
#define TEFRA_SKY_GLSL
// Night sky: procedural stars and the moon disc (requires gpu_types.glsl). Radiance is un-exposed, in the same units
// as the atmosphere sky (cd/m^2), so the exposure applies to it like everything else.

// Sin-free hash (Hoskins), as clouds.glsl.
float sk_hash(vec3 p) {
    vec3 p3 = fract(p * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Stars on an equal-area grid over the sphere (z = dir.y, phi = angle around +Y). Each cell holds one star with
// probability count / cells, at a random place in the cell. The 3x3 neighbourhood is searched so that a pixel sees
// every star within a cell of it. Night (f.stars.z) scales the result; stars fade out near the horizon.
vec3 sk_stars(Frame f, vec3 dir) {
    if (f.stars.z <= 0.0 || f.stars.x <= 0.0 || dir.y < -0.05) return vec3(0.0);
    const float NZ = 200.0, NP = 1256.0;
    const float TWO_PI = 6.2831853;
    const float prob = min(f.stars.x / (NZ * NP), 1.0);
    const vec2 g = vec2((dir.y * 0.5 + 0.5) * NZ, fract(atan(dir.z, dir.x) / TWO_PI) * NP);
    const vec2 c0 = floor(g);
    const float time = f.camera_pos.w;
    vec3 col = vec3(0.0);
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            float cz = c0.x + float(i);
            if (cz < 0.0 || cz >= NZ) continue;
            float cu = mod(c0.y + float(j) + NP, NP);
            if (sk_hash(vec3(cu, cz, 0.5)) > prob) continue;
            vec3 r = vec3(sk_hash(vec3(cu, cz, 1.5)), sk_hash(vec3(cu, cz, 2.5)), sk_hash(vec3(cu, cz, 3.5)));
            float zs = ((cz + r.x) / NZ) * 2.0 - 1.0;
            float ps = (cu + r.y) / NP * TWO_PI;
            float rs = sqrt(max(1.0 - zs * zs, 0.0));
            vec3 sd = vec3(rs * cos(ps), zs, rs * sin(ps));
            vec3 dd = sd - dir;
            const float sigma2 = 1.2e-6; // (0.0011 rad)^2: about a pixel at 1080p
            float mag = 0.2 + 0.8 * r.z * r.z * r.z * r.z; // most stars are faint
            vec3 tint = mix(vec3(1.0, 0.82, 0.62), vec3(0.72, 0.84, 1.0), sk_hash(vec3(cu, cz, 4.5)));
            float tw = 0.75 + 0.25 * sin(time * (1.5 + 3.0 * r.x) + r.y * 40.0);
            col += tint * mag * tw * exp(-dot(dd, dd) / sigma2);
        }
    }
    // 25 cd/m^2 for a bright star: about 0.06 in linear after the night exposure.
    return col * (25.0 * f.stars.y * f.stars.z) * smoothstep(-0.05, 0.2, dir.y);
}

// Moon disc with its phase: the disc is shaded as a sphere lit from the sun direction, in the moon's own frame
// (z along the moon's direction from the observer), with a faint earthshine on the dark side.
vec3 sk_moon(Frame f, vec3 dir) {
    if (f.celestial.y <= 0.0) return vec3(0.0);
    const vec3 M = normalize(f.moon.xyz);
    const float r = f.celestial.z;
    const float c = clamp(dot(dir, M), -1.0, 1.0);
    const float ang = acos(c);
    const float disc = 1.0 - smoothstep(r * 0.97, r, ang);
    if (disc <= 0.0) return vec3(0.0);
    vec3 T = normalize(cross(M, abs(M.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    vec3 B = cross(M, T);
    vec2 p = clamp(vec2(dot(dir, T), dot(dir, B)) / max(r, 1e-6), vec2(-1.0), vec2(1.0));
    vec3 n = vec3(p, -sqrt(max(1.0 - dot(p, p), 0.0))); // the face seen by the observer points back along M
    vec3 S = vec3(dot(f.sun_true.xyz, T), dot(f.sun_true.xyz, B), dot(f.sun_true.xyz, M));
    float lit = smoothstep(-0.02, 0.12, dot(n, normalize(S)));
    const vec3 albedo = vec3(0.92, 0.94, 1.0);
    return albedo * (2500.0 * lit + 0.6) * disc;
}
#endif
