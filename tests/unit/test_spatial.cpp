#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/physics/physics_world.h"
#include "tefra/physics/spatial.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/query.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::physics;
using namespace tefra::world;

namespace {

// A box for the math tests: centre, size and rotation in degrees (Y, X, Z).
Obb box(vec3 center, vec3 size, vec3 degrees = {}) {
    Obb b;
    b.center = center;
    b.rotation = orientation_to_quat(degrees);
    b.half = size * 0.5f;
    return b;
}

// A part with the given geometry, anchored so that nothing moves in the tests that use a world.
Instance* make_part(DataModel& dm, Instance* parent, const char* name, vec3 pos, vec3 size, u32 shape = 0,
                    vec3 degrees = {}) {
    Instance* p = dm.create("Part", parent, name);
    p->set("Position", Value::vector3(pos));
    p->set("Size", Value::vector3(size));
    p->set("Orientation", Value::vector3(degrees));
    p->set("Anchored", Value::boolean(true));
    p->set("Shape", Value::enumeration(shape));
    return p;
}

std::vector<std::string> names(const std::vector<Instance*>& parts) {
    std::vector<std::string> out;
    for (const Instance* p : parts) out.push_back(p->name());
    std::sort(out.begin(), out.end());
    return out;
}

OverlapRequest radius_query(vec3 center, f32 radius) {
    OverlapRequest q;
    q.kind = OverlapKind::Radius;
    q.center = center;
    q.radius = radius;
    return q;
}

} // namespace

TEST_CASE("spatial: a sphere touches an axis-aligned box only within its surface") {
    const Obb b = box({0, 0, 0}, {2, 2, 2});       // x, y, z in [-1, 1]
    CHECK(sphere_overlaps(b, {0, 0, 0}, 0.1f));    // centre inside
    CHECK(sphere_overlaps(b, {1.5f, 0, 0}, 0.6f)); // 0.5 m from the +X face
    CHECK_FALSE(sphere_overlaps(b, {1.5f, 0, 0}, 0.4f));
    CHECK(sphere_overlaps(b, {2.5f, 2.5f, 0}, 2.2f)); // nearest point is the edge (1, 1, 0): 2.12 m away
    CHECK_FALSE(sphere_overlaps(b, {2.5f, 2.5f, 0}, 2.0f));
}

TEST_CASE("spatial: a rotated box reaches its corners, an axis-aligned one does not") {
    // Rotated 45 degrees about Y (degrees are X, Y, Z), the square's corner points along +X at 1.414 m.
    const Obb rotated = box({0, 0, 0}, {2, 2, 2}, {0, 45, 0});
    CHECK(sphere_overlaps(rotated, {1.6f, 0, 0}, 0.2f));
    CHECK_FALSE(sphere_overlaps(rotated, {1.6f, 0, 0}, 0.15f));
    const Obb flat = box({0, 0, 0}, {2, 2, 2});
    CHECK_FALSE(sphere_overlaps(flat, {1.6f, 0, 0}, 0.2f));
}

TEST_CASE("spatial: a ball answers radius queries with its inscribed sphere") {
    Obb ball = box({0, 0, 0}, {2, 2, 2});
    ball.ball = true; // radius 1
    CHECK(sphere_overlaps(ball, {1.5f, 0, 0}, 0.6f));
    CHECK_FALSE(sphere_overlaps(ball, {1.5f, 0, 0}, 0.4f));
    // The box corner at (2.5, 2.5, 0) would count for a box (2.2 m away), not for the ball (3.54 m away).
    CHECK_FALSE(sphere_overlaps(ball, {2.5f, 2.5f, 0}, 2.2f));
}

TEST_CASE("spatial: separating-axis test for two boxes") {
    const Obb a = box({0, 0, 0}, {2, 2, 2});
    CHECK(obb_overlaps(a, box({1.5f, 0, 0}, {2, 2, 2})));
    CHECK(obb_overlaps(a, box({2.0f, 0, 0}, {2, 2, 2}))); // faces touch: counts as overlap
    CHECK_FALSE(obb_overlaps(a, box({2.5f, 0, 0}, {2, 2, 2})));
    CHECK(obb_overlaps(a, box({0, 2.9f, 0}, {4, 4, 4}))); // a big box around a small one
    // Rotated 45 degrees, the corner reaches 2.2 - 1.414 = 0.786 m into a: overlap. At 2.5 m it does not.
    CHECK(obb_overlaps(a, box({2.3f, 0, 0}, {2, 2, 2}, {0, 45, 0})));
    CHECK_FALSE(obb_overlaps(a, box({2.5f, 0, 0}, {2, 2, 2}, {0, 45, 0})));
    // Thin boxes that cross like a plus sign (the cross-product axes decide, not the face axes).
    CHECK(obb_overlaps(box({0, 0, 0}, {4, 0.2f, 0.2f}), box({0, 0, 0}, {0.2f, 4, 0.2f})));
}

TEST_CASE("spatial: volume and mass follow the shape and the density") {
    CHECK(part_volume(0, {1, 1, 1}) == doctest::Approx(1.0f));
    CHECK(part_volume(1, {2, 2, 2}) == doctest::Approx(4.18879f).epsilon(1e-4));
    CHECK(part_volume(2, {2, 2, 2}) == doctest::Approx(6.28319f).epsilon(1e-4));
    CHECK(part_volume(3, {2, 2, 2}) == doctest::Approx(4.0f));

    DataModel dm;
    Instance* block = make_part(dm, dm.workspace(), "Block", {0, 0, 0}, {1, 1, 1});
    CHECK(part_mass(*block) == doctest::Approx(700.0f)); // default density 700 kg/m^3
    block->set("Density", Value::number(0.0));
    CHECK(part_mass(*block) == doctest::Approx(1.0f)); // clamped to 1 kg/m^3
}

TEST_CASE("spatial: a radius query returns the parts whose box touches the sphere") {
    DataModel dm;
    Instance* ws = dm.workspace();
    make_part(dm, ws, "Near", {0, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "Mid", {0.8f, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "Far", {10, 0, 0}, {1, 1, 1});
    std::vector<Instance*> out;
    find_parts(dm, radius_query({0.6f, 0, 0}, 0.2f), out);
    CHECK(names(out) == std::vector<std::string>{"Mid", "Near"});
    find_parts(dm, radius_query({10, 0, 0}, 0.2f), out);
    CHECK(names(out) == std::vector<std::string>{"Far"});
    find_parts(dm, radius_query({5, 0, 0}, 1.0f), out);
    CHECK(out.empty());
}

TEST_CASE("spatial: a box query uses the orientation, and only Workspace parts are reported") {
    DataModel dm;
    Instance* ws = dm.workspace();
    make_part(dm, ws, "Tilted", {0, 0, 0}, {2, 2, 2}, 0, {0, 45, 0});
    make_part(dm, dm.service("ReplicatedStorage"), "Outside", {0, 0, 0}, {2, 2, 2});
    OverlapRequest q;
    q.kind = OverlapKind::Box;
    q.center = {1.5f, 0, 0};
    q.size = {0.2f, 0.2f, 0.2f};
    std::vector<Instance*> out;
    find_parts(dm, q, out);
    CHECK(names(out) == std::vector<std::string>{"Tilted"}); // its corner is at x = 1.414, inside x in [1.4, 1.6]
    q.center = {2.2f, 0, 0};
    find_parts(dm, q, out);
    CHECK(out.empty());
}

TEST_CASE("spatial: a part query returns the other parts it overlaps, never itself") {
    DataModel dm;
    Instance* ws = dm.workspace();
    Instance* near = make_part(dm, ws, "Near", {0, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "Mid", {0.8f, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "Far", {10, 0, 0}, {1, 1, 1});
    OverlapRequest q;
    q.kind = OverlapKind::Part;
    q.part = near;
    std::vector<Instance*> out;
    find_parts(dm, q, out);
    CHECK(names(out) == std::vector<std::string>{"Mid"});
}

TEST_CASE("spatial: MaxParts stops the query early") {
    DataModel dm;
    Instance* ws = dm.workspace();
    make_part(dm, ws, "A", {0, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "B", {0.2f, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "C", {0.1f, 0, 0}, {1, 1, 1});
    OverlapRequest q = radius_query({0, 0, 0}, 0.5f);
    q.max_parts = 2;
    std::vector<Instance*> out;
    find_parts(dm, q, out);
    CHECK(out.size() == 2u);
}

TEST_CASE("spatial: the Exclude and Include filters match a part by its ancestors") {
    DataModel dm;
    Instance* ws = dm.workspace();
    Instance* group = dm.create("Model", ws, "Group");
    make_part(dm, group, "Inside", {0, 0, 0}, {1, 1, 1});
    make_part(dm, ws, "Outside", {0.2f, 0, 0}, {1, 1, 1});

    QueryFilter exclude_group;
    exclude_group.instances = {group};
    OverlapRequest q = radius_query({0, 0, 0}, 0.5f);
    q.filter = &exclude_group;
    std::vector<Instance*> out;
    find_parts(dm, q, out);
    CHECK(names(out) == std::vector<std::string>{"Outside"});

    QueryFilter include_group;
    include_group.include = true;
    include_group.instances = {group};
    q.filter = &include_group;
    find_parts(dm, q, out);
    CHECK(names(out) == std::vector<std::string>{"Inside"});

    QueryFilter include_nothing;
    include_nothing.include = true;
    q.filter = &include_nothing;
    find_parts(dm, q, out);
    CHECK(out.empty());

    QueryFilter exclude_nothing; // an empty Exclude list filters nothing
    q.filter = &exclude_nothing;
    find_parts(dm, q, out);
    CHECK(out.size() == 2u);
}

// An unanchored part above a floor: a linear impulse lifts it, an angular one spins it, and the queued impulse
// applies at the next step. Spherecast and blockcast find the floor below the part.
TEST_CASE("physics: impulses move unanchored parts and casts find the floor") {
    DataModel dm;
    Instance* ws = dm.workspace();
    Instance* floor = make_part(dm, ws, "Floor", {0, -0.5f, 0}, {40, 1, 40});
    Instance* ball = make_part(dm, ws, "Ball", {0, 5, 0}, {1, 1, 1});
    ball->set("Anchored", Value::boolean(false));
    PhysicsWorld pw(dm);
    pw.step(1.0f / 60.0f);

    REQUIRE(pw.apply_impulse(*ball, {0, 700.0f * 5.0f, 0}, false)); // mass 700 kg: delta v = 5 m/s
    REQUIRE(pw.apply_impulse(*ball, {0, 0, 700.0f * 2.0f}, true));  // angular: 2 rad/s-ish spin on the ball
    const f32 y0 = ball->get_vec3("Position").y;
    for (int i = 0; i < 6; ++i) pw.step(1.0f / 60.0f); // 0.1 s
    CHECK(ball->get_vec3("Position").y > y0);          // it rose: 5 m/s up beats gravity over 0.1 s
    CHECK(length(ball->get_vec3("AssemblyAngularVelocity")) > 0.5f);
    for (int i = 0; i < 240; ++i) pw.step(1.0f / 60.0f); // 4 s: it has fallen back onto the floor
    CHECK(ball->get_vec3("Position").y == doctest::Approx(0.5f).epsilon(0.05));
    std::vector<Instance*> touching;
    pw.touching_parts(*ball, touching);
    CHECK(touching.size() == 1u);
    if (!touching.empty()) CHECK(touching[0] == floor);

    // A static part cannot take impulses.
    Instance* anchored = make_part(dm, ws, "Anchored", {20, 0, 0}, {1, 1, 1});
    CHECK_FALSE(pw.apply_impulse(*anchored, {0, 1, 0}, false));

    RaycastResult hit;
    // The casts go down at x = 5, clear of the ball (resting at the origin). A sphere of radius 0.5 falling from y = 3
    // meets the floor (top at y = 0) when its centre is at y = 0.5: 2.5 m.
    REQUIRE(pw.spherecast({5, 3, 0}, 0.5f, {0, -10, 0}, 10.0f, hit));
    CHECK(hit.instance == floor);
    CHECK(hit.distance == doctest::Approx(2.5f).epsilon(0.02));
    // Blockcast: a 1 m box tilted 45 degrees, from y = 3 down, meets the floor too.
    REQUIRE(pw.blockcast({5, 3, 0}, {0, 45, 0}, {1, 1, 1}, {0, -10, 0}, 10.0f, hit));
    CHECK(hit.instance == floor);
    CHECK(hit.normal.y > 0.99f);
    // A filter that excludes the floor finds nothing.
    QueryFilter no_floor;
    no_floor.instances = {floor};
    CHECK_FALSE(pw.spherecast({5, 3, 0}, 0.5f, {0, -10, 0}, 10.0f, hit, &no_floor));
}
