#pragma once

#include "tefra/core/math.h"

namespace tefra::render {

// First-person camera. World position in double precision; view matrix is camera-relative (no translation).
struct Camera {
    dvec3 position{0.0, 1.7, 0.0};
    f32 yaw = 0.0f;   // radians, 0 looks along -Z, positive turns left (counter-clockwise seen from above)
    f32 pitch = 0.0f; // radians, positive looks up
    f32 fovy = radians(60.0f);
    f32 z_near = 0.1f;

    vec3 forward() const;
    vec3 right() const;
    // Rotation-only view matrix (camera-relative rendering).
    mat4 view_rotation() const;
    mat4 projection(f32 aspect) const;
};

} // namespace tefra::render
