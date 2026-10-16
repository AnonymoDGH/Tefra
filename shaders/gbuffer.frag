#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
layout(push_constant) uniform Push {
    FrameBuffer frame; VertexBuffer vertices; InstanceBuffer instances; MaterialBuffer materials; uint cascade;
} pc;
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 4) flat in uint v_material;
layout(location = 5) flat in float v_rand; // per-instance random (vegetation), 0 for other meshes
layout(location = 6) in vec4 v_cur_clip;
layout(location = 7) in vec4 v_prev_clip;
layout(location = 8) flat in uint v_dynamic; // skinned: velocity is written, other meshes leave it at zero
layout(location = 0) out vec4 o_albedo;   // RGBA8_SRGB: base color, material AO
layout(location = 1) out vec4 o_normal;   // A2B10G10R10: oct normal, roughness, class
layout(location = 2) out vec4 o_material; // RGBA8: metallic, translucency, emissive, wetness
layout(location = 3) out vec2 o_velocity; // RG16F

// Material flag bit 2: bark. Bark gets a per-instance tint and moss (coverage in pad0, as float bits).
const uint kFlagBark = 4u;
const float kTrunkUvHeight = 1.6; // metres per unit of V on trunks (see add_trunk)

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
float value_noise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
float fbm3(vec3 p) {
    return 0.55 * value_noise(p) + 0.30 * value_noise(p * 2.03 + 7.1) + 0.15 * value_noise(p * 4.1 + 3.7);
}

#include "common/surface.glsl"
#include "common/rain_occlusion.glsl"

// Material flag bit 5: parallax occlusion mapping. Height (1 = top) in the normal map's alpha, depth in UV units in
// pad0 (float bits). Bit 6: interior mapping. The quad's UVs fall in one cell of a 4 x 3 room atlas; a ray is traced
// into a virtual box behind the glass (depth in cell widths in pad0) and the walls, floor and ceiling are looked up
// from that cell (van Dongen, "Interior Mapping", CGI 2008).
const uint kFlagParallax = 32u, kFlagInterior = 64u;
const float kPomNear = 22.0, kPomFade = 12.0;

vec2 parallax_uv(Material m, vec2 uv, vec3 Vts, float depth, vec2 dx, vec2 dy, out float self_ao) {
    self_ao = 1.0;
    const float steps = mix(20.0, 8.0, clamp(Vts.z, 0.0, 1.0));
    const vec2 delta = -Vts.xy / max(Vts.z, 0.2) * depth / steps;
    float layer = 1.0 / steps, cur = 1.0;
    vec2 p = uv;
    float h = textureGrad(TEX2D(m.normal_tex, SMP_ANISO_REPEAT), p, dx, dy).a;
    float prev_h = h, prev_cur = cur;
    for (int i = 0; i < 24 && h < cur; ++i) {
        prev_h = h;
        prev_cur = cur;
        p += delta;
        cur -= layer;
        h = textureGrad(TEX2D(m.normal_tex, SMP_ANISO_REPEAT), p, dx, dy).a;
    }
    // Linear interpolation between the last two layers (occlusion parallax).
    const float a = h - cur, b = prev_h - prev_cur;
    const float w = clamp(a / min(a - b, -1e-5), 0.0, 1.0);
    p = mix(p, p - delta, w);
    self_ao = mix(0.55, 1.0, h); // crevices read darker
    return p;
}

// Interior mapping: returns the atlas UV to shade with and a brightness factor for walls, floor and ceiling.
vec2 interior_uv(vec2 uv, vec3 Vts, float depth, out float shade, out float blur) {
    blur = 1.0;
    const vec2 grid = vec2(4.0, 3.0);
    const vec2 cell = floor(uv * grid);
    const vec2 local = clamp((fract(uv * grid) - 0.04) / 0.92, 0.0, 1.0);
    // Ray into the room (tangent space, +z inward). uv.y grows downwards on the wall (glTF flips V).
    const vec3 dir = normalize(vec3(-Vts.xy, max(Vts.z, 0.05)));
    const vec3 o = vec3(local, 0.0);
    const float tx = ((dir.x > 0.0 ? 1.0 : 0.0) - o.x) / (abs(dir.x) > 1e-4 ? dir.x : 1e-4);
    const float ty = ((dir.y > 0.0 ? 1.0 : 0.0) - o.y) / (abs(dir.y) > 1e-4 ? dir.y : 1e-4);
    const float tz = depth / dir.z;
    const float t = min(tz, min(tx, ty));
    const vec3 hit = o + dir * t;
    vec2 q;
    if (t == tz) {
        q = hit.xy; // back wall: the cell's picture
        shade = 1.0;
    } else if (t == tx) {
        q = vec2(dir.x > 0.0 ? 0.9 : 0.1, hit.y); // side wall: the cell's edge colours, blurred
        blur = 48.0;
        shade = mix(0.5, 0.28, hit.z / depth);
    } else if (dir.y > 0.0) {
        q = vec2(hit.x, 0.95); // floor (bottom of the picture)
        blur = 48.0;
        shade = mix(0.32, 0.2, hit.z / depth);
    } else {
        q = vec2(hit.x, 0.06); // ceiling (top of the picture, near the lamp)
        blur = 48.0;
        shade = mix(0.6, 0.4, hit.z / depth);
    }
    return (cell + 0.04 + clamp(q, 0.0, 1.0) * 0.92) / grid;
}

float emissive_hint(Material m) { return max(m.emissive_cutoff.r, max(m.emissive_cutoff.g, m.emissive_cutoff.b)); }

void main() {
    Material m = pc.materials.m[v_material];
    vec2 uv = v_uv;
    vec2 uv_dx = dFdx(v_uv), uv_dy = dFdy(v_uv);
    float pom_ao = 1.0, room_shade = 1.0;
    const bool interior = (m.flags & kFlagInterior) != 0u;
    if ((m.flags & (kFlagParallax | kFlagInterior)) != 0u) {
        const vec3 Nv = normalize(gl_FrontFacing ? v_normal : -v_normal);
        const vec3 Tv = normalize(v_tangent.xyz - Nv * dot(Nv, v_tangent.xyz));
        const vec3 Bv = cross(Nv, Tv) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
        const float dist = length(v_pos);
        const vec3 V = -v_pos / max(dist, 1e-4);
        const vec3 Vts = vec3(dot(V, Tv), dot(V, Bv), dot(V, Nv));
        const float depth = uintBitsToFloat(m.pad0);
        if (interior) {
            float blur;
            uv = interior_uv(v_uv, Vts, depth, room_shade, blur);
            // Walls, floor and ceiling read the cell's blurred colours (a coarse mip): no stretched streaks.
            uv_dx *= blur;
            uv_dy *= blur;
        } else if (dist < kPomNear && Vts.z > 0.0) {
            const float fade = 1.0 - smoothstep(kPomFade, kPomNear, dist);
            uv = parallax_uv(m, v_uv, Vts, depth * fade, uv_dx, uv_dy, pom_ao);
            pom_ao = mix(1.0, pom_ao, fade);
        }
    }
    vec4 base = textureGrad(TEX2D(m.base_color_tex, SMP_ANISO_REPEAT), uv, uv_dx, uv_dy) * m.base_color;
    base.rgb *= room_shade;
    if (m.emissive_cutoff.w > 0.0) {
        // Alpha-tested cards (palm fronds, needles, leaves): mip averaging thins the coverage with distance until
        // far crowns vanish. Boost alpha by the mip level and sharpen it to the pixel (Golus, "Anti-aliased alpha
        // test"), so distant foliage keeps its body.
        const float lod = textureQueryLod(TEX2D(m.base_color_tex, SMP_ANISO_REPEAT), v_uv).y;
        float a = base.a * (1.0 + max(lod, 0.0) * 0.25);
        a = (a - m.emissive_cutoff.w) / max(fwidth(a), 1e-4) + 0.5;
        if (a < 0.5) discard;
    }
    vec3 orm = textureGrad(TEX2D(m.orm_tex, SMP_ANISO_REPEAT), uv, uv_dx, uv_dy).rgb;
    float ao = mix(1.0, orm.r, m.occlusion_strength) * pom_ao;
    if ((m.flags & 1u) != 0u) ao *= clamp(abs(v_tangent.w), 0.0, 1.0); // foliage: baked crown occlusion
    float rough = clamp(orm.g * m.roughness, 0.02, 1.0);
    float metal = clamp(orm.b * m.metallic, 0.0, 1.0);
    vec4 nsamp = textureGrad(TEX2D(m.normal_tex, SMP_ANISO_REPEAT), uv, uv_dx, uv_dy);
    if (interior) nsamp = vec4(0.5, 0.5, 1.0, 1.0); // the virtual room is flat
    vec3 tn = nsamp.xyz * 2.0 - 1.0;
    tn.xy *= m.normal_scale;

    float moss = 0.0;
    if ((m.flags & kFlagBark) != 0u) {
        // Per-instance tint: slightly darker or lighter, warmer or cooler.
        base.rgb *= mix(vec3(0.86, 0.80, 0.74), vec3(1.10, 1.02, 0.94), v_rand);
        // Moss: periodic in angle (from the horizontal normal, so no seam), patchy, on the base and on the
        // north side (world -Z). Height comes from V (V = y / kTrunkUvHeight, the trunk axis).
        const float hgt = max(v_uv.y * kTrunkUvHeight, 0.0);
        const vec2 hn = normalize(v_normal.xz + vec2(1e-5));
        const float north = dot(hn, vec2(0.0, -1.0));
        const float orient = smoothstep(-0.2, 0.8, north);
        const vec3 q = vec3(hn.x, hn.y, hgt * 0.6) * 2.5 + vec3(v_rand * 7.3);
        const float patchy = fbm3(q);
        const float fine = value_noise(q * 4.0 + 11.0);
        const float base_w = 1.0 - smoothstep(1.0, 2.8, hgt + (patchy - 0.5) * 1.5);
        const float side_w = mix(0.4, 1.0, orient);
        const float clump = smoothstep(0.36, 0.52, 0.7 * patchy + 0.3 * fine);
        moss = clamp(uintBitsToFloat(m.pad0) * base_w * side_w * clump, 0.0, 1.0);
        const vec3 moss_col = mix(vec3(0.030, 0.075, 0.018), vec3(0.090, 0.150, 0.045), fine);
        base.rgb = mix(base.rgb, moss_col, moss * 0.9);
        tn = mix(tn, vec3(0.0, 0.0, 1.0), moss * 0.6); // moss is soft: it hides the bark relief
    }

    // Part materials: procedural world-space surface (tint, relief, roughness).
    const vec3 P = v_pos + pc.frame.f.camera_pos.xyz;
    const float footprint = length(fwidth(P));
    float surf_h = 0.0;
    vec3 surf_emit = vec3(0.0);
    const bool surf = (m.flags & kFlagSurface) != 0u;
    g_surface_time = pc.frame.f.camera_pos.w;
    if (surf) {
        const vec3 ng = gl_FrontFacing ? normalize(v_normal) : -normalize(v_normal);
        const Surface sp = surface_pattern(m.pad1, P, ng, footprint);
        base.rgb = clamp(base.rgb * sp.tint, 0.0, 1.0);
        rough = clamp(rough * sp.rough, 0.02, 1.0);
        ao *= sp.ao;
        surf_h = sp.height;
        surf_emit = sp.emit;
    }

    // Rain (Lighting.Rain): wet surfaces darken and turn glossy; standing water collects in puddles on flat ground,
    // mirror-smooth, with rings from the drops.
    const vec4 weather = pc.frame.f.weather;
    float puddle = 0.0;
    vec3 ripple = vec3(0.0);
    if (weather.x > 0.0 && (m.flags & 1u) == 0u && emissive_hint(m) == 0.0 && !interior) {
        const vec3 ngw = gl_FrontFacing ? normalize(v_normal) : -normalize(v_normal);
        const float up = smoothstep(0.75, 0.97, ngw.y);
        // Sheltered (indoors, under a roof or an awning): dry. Walls test a point just in front of them.
        const float open_sky = rain_visibility(pc.frame.f, v_pos + ngw * mix(0.4, 0.05, up));
        const float porous = 1.0 - metal;
        float pn = fbm3(vec3(P.x * 0.21, 0.0, P.z * 0.21)) + 0.12 * value_noise(vec3(P.x * 1.7, 2.0, P.z * 1.7));
        float th = 1.0 - weather.y * 0.55;
        puddle = up * smoothstep(th - 0.06, th + 0.04, pn) * weather.x * open_sky;
        float wet = weather.x * mix(0.65, 1.0, up) * open_sky;
        base.rgb *= mix(1.0, mix(0.6, 0.4, puddle), wet * porous);
        // A porous surface (concrete, asphalt, plaster) only gets a thin, broken water film: it turns satin, not
        // polished. Damp patches stay rough (~0.5), saturated ones reach ~0.3; only standing water is a mirror
        // (Lagarde, "Water drop 2b - Dynamic rain and its effects").
        float film = smoothstep(0.35, 0.75, value_noise(vec3(P.x * 0.9, 5.0, P.z * 0.9)) * 0.6 + pn * 0.6);
        float wet_rough = max(rough * mix(0.75, 0.55, film), mix(0.5, 0.3, film));
        rough = mix(rough, mix(min(rough, wet_rough), 0.03, puddle), wet * mix(porous, 1.0, puddle));
        // Water fills the relief.
        tn = mix(tn, vec3(0.0, 0.0, 1.0), puddle);
        surf_h *= 1.0 - puddle;
        if (weather.z > 0.0 && puddle > 0.05) {
            // Drop rings: a 2x2 neighbourhood of 30 cm cells, each with its own phase.
            vec2 q = P.xz / 0.3;
            vec2 ci = floor(q);
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    vec2 c = ci + vec2(i, j);
                    float h = hash13(vec3(c, 7.0));
                    float t = fract(pc.frame.f.camera_pos.w * (0.9 + 0.6 * h) + h * 13.0);
                    vec2 ctr = c + vec2(hash13(vec3(c, 1.0)), hash13(vec3(c, 2.0)));
                    vec2 dv = q - ctr;
                    float r = length(dv);
                    float ring = t * 1.2;
                    float a = (1.0 - t) * exp(-pow((r - ring) * 10.0, 2.0)) * step(h, weather.z);
                    ripple.xz += dv / max(r, 1e-3) * a * cos((r - ring) * 30.0) * 0.35;
                }
            ripple *= puddle;
        }
    }

    if ((m.flags & 8u) != 0u) ao *= nsamp.a; // needles: depth AO baked into the normal map's alpha
    vec3 N = normalize(v_normal);
    if (!gl_FrontFacing) N = -N;
    const vec3 Ng = N; // crown (spherical) normal
    vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
    vec3 B = cross(N, T) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
    N = normalize(T * tn.x + B * tn.y + N * max(tn.z, 1e-3));
    if (surf) N = bump_from_height(N, P, surf_h, 1.0);
    if (puddle > 0.0) N = normalize(mix(N, Ng, puddle) + ripple);
    // Moss: rough and non-metallic, so it has almost no specular.
    rough = mix(rough, 0.95, moss);
    metal = mix(metal, 0.0, moss);
    // Foliage: keep part of the crown normal so clusters of cards still shade as volumes, not as flat leaves.
    if ((m.flags & 1u) != 0u) N = normalize(mix(N, Ng, 0.35));
    // Geometric specular AA (Tokuyoshi & Kaplanyan 2019).
    vec3 dndx = dFdx(N), dndy = dFdy(N);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    float a2 = rough * rough * rough * rough;
    a2 = clamp(a2 + min(2.0 * variance, 0.18), 0.0, 1.0);
    rough = sqrt(sqrt(a2));
    // Emission (Neon parts, glTF emissive): strength 0..1 in the G-buffer, its colour replaces the albedo there.
    vec3 emit = m.emissive_cutoff.rgb;
    if (m.emissive_tex != 0xFFFFFFFFu && dot(emit, emit) > 0.0)
        emit *= textureGrad(TEX2D(m.emissive_tex, SMP_ANISO_REPEAT), uv, uv_dx, uv_dy).rgb * room_shade;
    emit += surf_emit;
    float emissive = clamp(max(emit.r, max(emit.g, emit.b)), 0.0, 1.0);
    if (emissive > 0.0) base.rgb = mix(base.rgb, emit / max(emissive, 1e-4), emissive);
    o_albedo = vec4(base.rgb, ao);
    o_normal = vec4(oct_encode01(N), rough, (m.flags & 1u) != 0u ? 1.0 / 3.0 : 0.0);
    o_material = vec4(metal, m.translucency, emissive, 0.0);
    // Screen-space motion in uv units (previous minus current, both unjittered), read by TAA for dynamic pixels.
    vec2 cur_ndc = v_cur_clip.xy / v_cur_clip.w, prev_ndc = v_prev_clip.xy / v_prev_clip.w;
    o_velocity = v_dynamic != 0u ? (prev_ndc - cur_ndc) * 0.5 : vec2(0.0);
}
