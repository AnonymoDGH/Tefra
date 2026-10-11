#ifndef TEFRA_BRDF_GLSL
#define TEFRA_BRDF_GLSL
const float BRDF_PI = 3.14159265358979;
float D_GGX(float ndh, float a) {
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / (BRDF_PI * d * d);
}
// Height-correlated Smith visibility (includes the 1/(4 nl nv) term).
float V_SmithGGX(float ndv, float ndl, float a) {
    float a2 = a * a;
    float gv = ndl * sqrt(ndv * ndv * (1.0 - a2) + a2);
    float gl = ndv * sqrt(ndl * ndl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}
vec3 F_Schlick(vec3 f0, float vdh) { return f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0); }
#endif
