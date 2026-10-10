#version 460
#extension GL_GOOGLE_include_directive : require
#include "common/gpu_types.glsl"
#include "common/bindless.glsl"
// Temporal anti-aliasing: depth reprojection (static world), Catmull-Rom history, variance clipping in YCoCg,
// luminance-weighted blend (Karis 2014, Salvi 2016).
layout(push_constant) uniform Push { FrameBuffer frame; uint current; uint history; uint depth; uint valid; vec4 sizes; } pc;
// sizes: xy = input (render) size, zw = output size. Input may be smaller than output (TAAU).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

vec3 to_ycocg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 from_ycocg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

vec3 fetch(ivec2 p) {
    ivec2 size = ivec2(pc.sizes.xy);
    return texelFetch(TEX2D(pc.current, SMP_NEAREST_CLAMP), clamp(p, ivec2(0), size - 1), 0).rgb;
}

// 5-tap Catmull-Rom (bilinear-optimized, Jimenez).
vec3 history_catmull(vec2 uv, vec2 size) {
    vec2 pos = uv * size, tc1 = floor(pos - 0.5) + 0.5, f = pos - tc1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f)), w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2, tc0 = (tc1 - 1.0) / size, tc3 = (tc1 + 2.0) / size, tc12 = (tc1 + w2 / w12) / size;
    vec3 r = vec3(0.0);
    r += textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), vec2(tc12.x, tc0.y), 0.0).rgb * (w12.x * w0.y);
    r += textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), vec2(tc0.x, tc12.y), 0.0).rgb * (w0.x * w12.y);
    r += textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), vec2(tc12.x, tc12.y), 0.0).rgb * (w12.x * w12.y);
    r += textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), vec2(tc3.x, tc12.y), 0.0).rgb * (w3.x * w12.y);
    r += textureLod(TEX2D(pc.history, SMP_LINEAR_CLAMP), vec2(tc12.x, tc3.y), 0.0).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, vec3(0.0));
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
    Frame f = pc.frame.f;
    // Output pixel -> input texel (identity without upscaling).
    vec2 in_pos = v_uv * pc.sizes.xy;
    ivec2 px = ivec2(in_pos);
    vec3 cur = pc.sizes.x < pc.sizes.z ? textureLod(TEX2D(pc.current, SMP_LINEAR_CLAMP), v_uv, 0.0).rgb : fetch(px);
    if (pc.valid == 0u) { o_color = vec4(cur, 1.0); return; }

    // Closest depth in a 3x3 neighbourhood: reprojects edges with the foreground's motion.
    float best = 0.0;
    vec3 m1 = vec3(0.0), m2 = vec3(0.0), mn = vec3(1e30), mx = vec3(-1e30);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = px + ivec2(x, y);
            best = max(best, texelFetch(TEX2D(pc.depth, SMP_NEAREST_CLAMP), clamp(q, ivec2(0), ivec2(pc.sizes.xy) - 1), 0).r); // reverse-Z
            vec3 c = to_ycocg(fetch(q));
            m1 += c;
            m2 += c * c;
            mn = min(mn, c);
            mx = max(mx, c);
        }
    vec2 ndc = v_uv * 2.0 - 1.0; // the jittered inverse maps pixel centers to what they actually show
    vec4 hp = f.inv_view_proj * vec4(ndc, max(best, 1e-7), 1.0);
    vec3 p = hp.xyz / hp.w;
    if (best <= 0.0) p = normalize(p) * 1e6; // sky: rotation-only reprojection
    vec4 prev = f.prev_view_proj * vec4(p, 1.0);
    vec2 prev_uv = (prev.xy / prev.w) * 0.5 + 0.5;
    vec2 size = pc.sizes.zw;
    if (any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0)))) { o_color = vec4(cur, 1.0); return; }

    vec3 hist = to_ycocg(history_catmull(prev_uv, size));
    vec3 mean = m1 / 9.0, sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec3 lo = max(mn, mean - 1.25 * sigma), hi = min(mx, mean + 1.25 * sigma);
    // Clip towards the mean (Playdead) instead of clamping: fewer colour shifts.
    vec3 d = hist - mean, e = 0.5 * (hi - lo) + 1e-5;
    vec3 a = abs(d / e);
    float k = max(a.x, max(a.y, a.z));
    if (k > 1.0) hist = mean + d / k;
    vec3 h = from_ycocg(hist);
    float alpha = pc.sizes.x < pc.sizes.z ? 0.08 : 0.1;
    float wc = alpha / (1.0 + luma(cur)), wh = (1.0 - alpha) / (1.0 + luma(h));
    o_color = vec4(max((cur * wc + h * wh) / (wc + wh), vec3(0.0)), 1.0);
}
