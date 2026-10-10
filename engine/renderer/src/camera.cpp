#include "tefra/renderer/camera.h"

namespace tefra::render {

vec3 Camera::forward() const {
    const f32 cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

vec3 Camera::right() const { return normalize(cross(forward(), vec3{0.0f, 1.0f, 0.0f})); }

mat4 Camera::view_rotation() const { return look_at(vec3{}, forward(), vec3{0.0f, 1.0f, 0.0f}); }

mat4 Camera::projection(f32 aspect) const { return perspective_reverse_z(fovy, aspect, z_near); }

} // namespace tefra::render
