#ifndef TEFRA_AGX_GLSL
#define TEFRA_AGX_GLSL
// AgX display transform (Troy Sobotka), with the polynomial sigmoid fit by Benjamin Wrensch.
// Input: linear Rec.709 scene-referred. Output: linear (approx.) for an sRGB-encoded target.
vec3 agx_contrast(vec3 x) {
    vec3 x2 = x * x, x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 agx(vec3 c) {
    const mat3 m = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                        0.0784335999999992, 0.878468636469772, 0.0784336,
                        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 mi = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                         -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                         -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float min_ev = -12.47393, max_ev = 4.026069;
    c = m * max(c, vec3(1e-10));
    c = clamp(log2(c), min_ev, max_ev);
    c = (c - min_ev) / (max_ev - min_ev);
    c = agx_contrast(c);
    c = mi * c;
    return pow(max(c, vec3(0.0)), vec3(2.2));
}
#endif
