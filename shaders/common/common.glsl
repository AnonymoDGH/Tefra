#ifndef TEFRA_COMMON_GLSL
#define TEFRA_COMMON_GLSL

const float PI = 3.14159265358979323846;

// Integer hash (PCG-like), deterministic across GPUs.
uint hash_u32(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float hash_2d(uvec2 p) { return float(hash_u32(p.x ^ hash_u32(p.y))) * (1.0 / 4294967296.0); }

#endif
