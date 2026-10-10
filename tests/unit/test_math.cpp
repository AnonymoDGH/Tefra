#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/math.h"

using namespace tefra;

static bool near(f32 a, f32 b, f32 eps = 1e-5f) { return std::fabs(a - b) <= eps; }
static bool near(vec3 a, vec3 b, f32 eps = 1e-5f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
static bool near(const mat4& a, const mat4& b, f32 eps = 1e-5f) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!near(a.at(r, c), b.at(r, c), eps)) return false;
    return true;
}

TEST_CASE("vec3 basics") {
    const vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK(dot(a, b) == doctest::Approx(32.0f));
    CHECK(near(cross(vec3{1, 0, 0}, vec3{0, 1, 0}), vec3{0, 0, 1})); // right-handed
    CHECK(near(normalize(vec3{3, 0, 4}), vec3{0.6f, 0, 0.8f}));
    CHECK(near(normalize(vec3{}), vec3{}));
    CHECK(length(vec3{2, 3, 6}) == doctest::Approx(7.0f));
}

TEST_CASE("mat4 multiply and inverse") {
    const mat4 t = translate({1, 2, 3});
    const mat4 s = scale({2, 2, 2});
    const mat4 r = mat4_from_quat(quat_axis_angle({0, 1, 0}, 0.7f));
    const mat4 m = t * r * s;
    CHECK(near(m * inverse(m), mat4::identity(), 1e-5f));
    CHECK(near(transform_point(t, {0, 0, 0}), vec3{1, 2, 3}));
    CHECK(near(transform_dir(t, {1, 0, 0}), vec3{1, 0, 0}));
    CHECK(near(transpose(transpose(m)), m));
    CHECK(near(compose_trs({1, 2, 3}, quat_axis_angle({0, 1, 0}, 0.7f), {2, 2, 2}), m, 1e-5f));
}

TEST_CASE("quaternion rotation matches matrix") {
    const quat q = quat_axis_angle({0, 0, 1}, kHalfPi);
    CHECK(near(rotate(q, {1, 0, 0}), vec3{0, 1, 0}));
    CHECK(near(transform_dir(mat4_from_quat(q), {1, 0, 0}), vec3{0, 1, 0}));
    const quat q2 = normalize(q * q);
    CHECK(near(rotate(q2, {1, 0, 0}), vec3{-1, 0, 0}, 1e-5f));
}

TEST_CASE("look_at: camera looks down -Z in view space") {
    const mat4 v = look_at({0, 1.7f, 5}, {0, 1.7f, 0}, {0, 1, 0});
    CHECK(near(transform_point(v, {0, 1.7f, 0}), vec3{0, 0, -5}));
    CHECK(near(transform_point(v, {1, 1.7f, 5}), vec3{1, 0, 0})); // +X world is to the right
}

TEST_CASE("perspective_reverse_z: near maps to 1, infinity to 0, y flipped for Vulkan") {
    const mat4 p = perspective_reverse_z(radians(90.0f), 1.0f, 0.1f);
    vec4 c = p * vec4(0, 0, -0.1f, 1);
    CHECK(c.z / c.w == doctest::Approx(1.0f));
    c = p * vec4(0, 0, -1e7f, 1);
    CHECK(c.z / c.w == doctest::Approx(0.0f).epsilon(1e-6));
    c = p * vec4(0, 1, -1, 1); // above the axis, 45 deg -> top edge of the screen
    CHECK(c.y / c.w == doctest::Approx(-1.0f));
    const mat4 inv = inverse(p);
    const vec4 back = inv * vec4(0, 0, 1, 1);
    CHECK(back.z / back.w == doctest::Approx(-0.1f));
}

TEST_CASE("camera-relative positions keep precision far from the origin") {
    const dvec3 cam{16000.25, 3.0, -16000.5};
    const dvec3 p{16000.75, 4.5, -16001.0};
    const vec3 r = relative_to(p, cam);
    CHECK(near(r, vec3{0.5f, 1.5f, -0.5f}, 1e-6f));
}
