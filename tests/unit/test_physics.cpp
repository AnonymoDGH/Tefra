#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/physics/character.h"
#include "tefra/physics/physics_world.h"
#include "tefra/world/datamodel.h"

#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::physics;
using namespace tefra::world;

namespace {

constexpr u32 kBlock = 0, kBall = 1;

// A model with a Workspace and a physics world; helpers create parts and run the simulation at 60 Hz.
struct Sim {
    DataModel dm;
    PhysicsWorld pw{dm};
    Instance* ws = dm.workspace();

    Instance* part(const char* name, vec3 pos, vec3 size, bool anchored, u32 shape = kBlock, bool collide = true) {
        Instance* p = dm.create("Part", ws, name);
        p->set("Position", Value::vector3(pos));
        p->set("Size", Value::vector3(size));
        p->set("Anchored", Value::boolean(anchored));
        p->set("Shape", Value::enumeration(shape));
        p->set("CanCollide", Value::boolean(collide));
        return p;
    }
    void run(f32 seconds) {
        const int steps = static_cast<int>(seconds / kPhysicsStep + 0.5f);
        for (int i = 0; i < steps; ++i) pw.step(kPhysicsStep);
    }
};

bool near(f32 a, f32 b, f32 eps) { return a > b - eps && a < b + eps; }

} // namespace

TEST_CASE("physics: dynamic part lands on an anchored floor at the expected height") {
    Sim s;
    Instance* floor = s.part("Floor", {0, -0.5f, 0}, {40, 1, 40}, true);
    Instance* box = s.part("Box", {0, 5, 0}, {2, 1, 2}, false);
    s.run(3.0f);
    CHECK(s.pw.body_count() == 2);
    CHECK(s.pw.has_body(*floor));
    CHECK(near(box->get_vec3("Position").y, 0.5f, 0.02f)); // half height of the box on the top face (y = 0)
    CHECK(near(box->get_vec3("AssemblyLinearVelocity").y, 0.0f, 0.05f));
    CHECK(floor->get_vec3("Position").y == doctest::Approx(-0.5f)); // anchored parts never move
}

TEST_CASE("physics: a world created after the parts simulates them") {
    DataModel dm;
    Instance* ws = dm.workspace();
    Instance* floor = dm.create("Part", ws, "Floor");
    floor->set("Size", Value::vector3({20, 1, 20}));
    floor->set("Position", Value::vector3({0, -0.5f, 0}));
    floor->set("Anchored", Value::boolean(true));
    Instance* box = dm.create("Part", ws, "Box");
    box->set("Position", Value::vector3({0, 3, 0}));
    PhysicsWorld pw(dm);
    CHECK(pw.body_count() == 0); // bodies appear on the first sync
    pw.sync();
    CHECK(pw.body_count() == 2);
    for (int i = 0; i < 120; ++i) pw.step(kPhysicsStep);
    CHECK(near(box->get_vec3("Position").y, 0.5f, 0.02f));
}

TEST_CASE("physics: Touched fires for a landing pair and for a sensor") {
    Sim s;
    s.part("Floor", {0, -0.5f, 0}, {40, 1, 40}, true);
    s.part("Box", {0, 5, 0}, {2, 1, 2}, false);
    std::vector<std::string> pairs;
    s.pw.add_touched_listener([&](Instance& a, Instance& b, bool ended) {
        if (!ended) pairs.push_back(a.name() + "+" + b.name());
    });
    s.run(2.0f);
    bool landed = false;
    for (const std::string& p : pairs) landed = landed || p == "Box+Floor" || p == "Floor+Box";
    CHECK(landed);

    // A sensor (CanCollide=false) is touched but does not stop the ball, which rests on the floor.
    Sim t;
    t.part("Floor", {0, -0.5f, 0}, {40, 1, 40}, true);
    t.part("Gate", {0, 2, 0}, {4, 0.2f, 4}, true, kBlock, false);
    Instance* ball = t.part("Ball", {0, 4, 0}, {1, 1, 1}, false, kBall);
    std::vector<std::string> sensor_pairs;
    t.pw.add_touched_listener([&](Instance& a, Instance& b, bool ended) {
        if (!ended) sensor_pairs.push_back(a.name() + "+" + b.name());
    });
    t.run(2.5f);
    bool sensed = false;
    for (const std::string& p : sensor_pairs) sensed = sensed || p == "Gate+Ball" || p == "Ball+Gate";
    CHECK(sensed);
    CHECK(near(ball->get_vec3("Position").y, 0.5f, 0.02f));
}

TEST_CASE("physics: ball rolls down a wedge ramp") {
    Sim s;
    s.part("Floor", {0, -0.5f, 0}, {60, 1, 20}, true);
    // Wedge: slope rises towards local +Z. Orientation Y = -90 turns it so the high side faces -X.
    Instance* ramp = s.part("Ramp", {0, 1.5f, 0}, {6, 3, 12}, true, 3);
    ramp->set("Orientation", Value::vector3({0, -90, 0}));
    Instance* ball = s.part("Ball", {-4, 4, 0}, {1, 1, 1}, false, kBall);
    s.run(3.0f);
    CHECK(ball->get_vec3("Position").x > 1.0f);
    CHECK(ball->get_vec3("Position").y < 2.0f);
    CHECK(ball->get_vec3("Position").y > 0.0f);
}

TEST_CASE("physics: a ball knocks a stack of light bricks apart") {
    Sim s;
    s.part("Ground", {0, -0.5f, 0}, {80, 1, 80}, true);
    std::vector<std::pair<Instance*, vec3>> bricks;
    for (u32 layer = 0; layer < 4; ++layer) {
        const f32 y = 0.5f + static_cast<f32>(layer);
        for (int k = -1; k <= 1; ++k) {
            const f32 o = static_cast<f32>(k);
            Instance* b = layer % 2 == 0 ? s.part("Brick", {o, y, 0}, {1, 1, 3}, false)
                                         : s.part("Brick", {0, y, o}, {3, 1, 1}, false);
            b->set("Density", Value::number(200.0));
            bricks.emplace_back(b, b->get_vec3("Position"));
        }
    }
    Instance* ball = s.part("Ball", {-16, 1.5f, 0}, {3, 3, 3}, false, kBall);
    ball->set("Density", Value::number(600.0)); // about 8.5 t
    ball->set("AssemblyLinearVelocity", Value::vector3({7, 0, 0}));
    s.run(4.0f);
    u32 moved = 0;
    for (const auto& [b, start] : bricks)
        if (length(b->get_vec3("Position") - start) > 0.5f) ++moved;
    CHECK(moved >= 3);
    CHECK(ball->get_vec3("Position").x > -16.0f); // the ball did not stop at the start
}

TEST_CASE("physics: raycast hits the nearest solid part and skips sensors") {
    Sim s;
    Instance* target = s.part("Target", {0, 1, -10}, {2, 2, 2}, true);
    s.part("Gate", {0, 1, -5}, {2, 2, 2}, true, kBlock, false);
    s.pw.sync();
    RaycastResult hit;
    REQUIRE(s.pw.raycast({0, 1, 0}, {0, 0, -1}, 100.0f, hit));
    CHECK(hit.instance == target);
    CHECK(hit.distance == doctest::Approx(9.0f).epsilon(0.01));
    CHECK(hit.position.z == doctest::Approx(-9.0f).epsilon(0.01));
    CHECK(hit.normal.z == doctest::Approx(1.0f).epsilon(0.01));
    CHECK_FALSE(s.pw.raycast({0, 1, 0}, {0, 0, -1}, 5.0f, hit));  // too short
    CHECK_FALSE(s.pw.raycast({0, 1, 0}, {0, 1, 0}, 100.0f, hit)); // nothing above
}

TEST_CASE("physics: moving a part in the model moves its body; destroying it removes the body") {
    Sim s;
    s.part("Floor", {0, -0.5f, 0}, {40, 1, 40}, true);
    Instance* box = s.part("Box", {0, 5, 0}, {1, 1, 1}, false);
    s.run(0.1f);
    box->set("Position", Value::vector3({0, 10, 0}));
    s.run(0.05f);
    CHECK(box->get_vec3("Position").y > 9.5f); // teleported, then falling
    CHECK(s.pw.has_body(*box));
    s.dm.destroy(box);
    s.pw.sync();
    CHECK(s.pw.body_count() == 1);
}

namespace {

// Floor (top at y = 0) and a Model with a Humanoid and a 2 x 2 x 1 HumanoidRootPart standing on it.
struct Hero {
    Instance* model;
    Instance* hum;
    Instance* root;
    Hero(Sim& s, vec3 feet_xz) {
        s.part("Floor", {0, -0.5f, 0}, {200, 1, 40}, true);
        model = s.dm.create("Model", s.ws, "Hero");
        hum = s.dm.create("Humanoid", model);
        root = s.dm.create("Part", model, "HumanoidRootPart");
        root->set("Size", Value::vector3({2, 2, 1}));
        root->set("Position", Value::vector3({feet_xz.x, 1.9f, feet_xz.z})); // HipHeight 0.9 + 2 / 2 above feet
    }
};

u32 state_of(Instance* hum) { return hum->get("State")->as_enum(); }

u32 state_named(const char* name) { return ClassRegistry::get().find_enum("HumanoidState")->index_of(name); }

} // namespace

TEST_CASE("physics: character walks at WalkSpeed, jumps to JumpHeight and lands") {
    Sim s;
    Hero h(s, vec3{0, 0, 0});
    CharacterController cc(s.pw, *h.model);
    REQUIRE(cc.valid());
    s.run(1.0f);
    CHECK(cc.on_ground());
    CHECK(near(cc.position().y, 0.0f, 0.05f));

    cc.set_move_direction({1, 0, 0});
    const f32 x0 = cc.position().x;
    s.run(3.0f);
    CHECK(cc.position().x - x0 == doctest::Approx(h.hum->get_float("WalkSpeed") * 3.0f).epsilon(0.03));
    CHECK(near(cc.position().y, 0.0f, 0.05f));
    CHECK(state_of(h.hum) == state_named("Running"));
    // Facing: forward is -Z; after yaw -90 it must point along +X.
    const vec3 forward = rotate(orientation_to_quat(h.root->get_vec3("Orientation")), {0, 0, -1});
    CHECK(forward.x == doctest::Approx(1.0f).epsilon(0.02));

    cc.set_move_direction({0, 0, 0});
    s.run(1.0f);
    CHECK(state_of(h.hum) == state_named("Idle"));

    cc.jump();
    f32 peak = 0.0f;
    bool saw_jumping = false, saw_freefall = false;
    for (int i = 0; i < 90; ++i) {
        s.run(kPhysicsStep);
        peak = max(peak, cc.position().y);
        saw_jumping = saw_jumping || state_of(h.hum) == state_named("Jumping");
        saw_freefall = saw_freefall || state_of(h.hum) == state_named("Freefall");
    }
    CHECK(peak == doctest::Approx(h.hum->get_float("JumpHeight")).epsilon(0.07));
    CHECK(saw_jumping);
    CHECK(saw_freefall);
    CHECK(cc.on_ground());
    CHECK(near(cc.position().y, 0.0f, 0.05f));
    CHECK(state_of(h.hum) == state_named("Idle"));
}

TEST_CASE("physics: character climbs a 0.3 m step") {
    Sim s;
    Hero h(s, vec3{-5, 0, 0});
    s.part("Step", {10, 0.15f, 0}, {20, 0.3f, 20}, true); // top at y = 0.3, from x = 0
    CharacterController cc(s.pw, *h.model);
    REQUIRE(cc.valid());
    s.run(0.5f);
    cc.set_move_direction({1, 0, 0});
    s.run(3.0f); // 18 m: from x = -5 to x = 13
    CHECK(cc.position().x > 5.0f);
    CHECK(cc.position().y == doctest::Approx(0.3f).epsilon(0.05));
}