#include <doctest.h>

#include "tefra/world/datamodel.h"
#include "tefra/world/particles.h"

using namespace tefra;
using namespace tefra::world;

TEST_CASE("particles: rate, lifetime, bursts and sorting") {
    DataModel dm;
    Instance* part = dm.create("Part", dm.workspace(), "Emitter");
    part->set("Position", Value::vector3({0, 5, 0}));
    Instance* e = dm.create("ParticleEmitter", part, "Fx");
    CHECK(e != nullptr);
    if (!e) return;
    e->set("Rate", Value::number(100.0));
    e->set("Lifetime", Value::number(1.0));
    e->set("Variation", Value::number(0.0));
    e->set("Speed", Value::number(2.0));
    e->set("TransparencyEnd", Value::number(0.0));

    ParticleSystem ps;
    for (int i = 0; i < 50; ++i) ps.update(dm, 0.01f, {0, 5, 20});
    CHECK(ps.count() >= 45);
    CHECK(ps.count() <= 51);
    // Steady state: rate x lifetime.
    for (int i = 0; i < 200; ++i) ps.update(dm, 0.01f, {0, 5, 20});
    CHECK(ps.count() >= 95);
    CHECK(ps.count() <= 101);
    // Back to front from the camera.
    const auto& g = ps.gpu();
    for (usize i = 1; i < g.size(); ++i) {
        const vec3 pa = vec3(g[i - 1].pos_size.x, g[i - 1].pos_size.y, g[i - 1].pos_size.z) - vec3{0, 5, 20};
        const vec3 pb = vec3(g[i].pos_size.x, g[i].pos_size.y, g[i].pos_size.z) - vec3{0, 5, 20};
        CHECK(dot(pa, pa) >= dot(pb, pb));
    }
    // Disabled: no new particles; the old ones die out. A burst still emits.
    e->set("Enabled", Value::boolean(false));
    for (int i = 0; i < 120; ++i) ps.update(dm, 0.01f, {0, 5, 20});
    CHECK(ps.count() == 0);
    e->set("EmitCount", Value::integer(30));
    ps.update(dm, 0.01f, {0, 5, 20});
    CHECK(ps.count() == 30);
    CHECK(e->get("EmitCount")->as_int() == 0);
    // Cap.
    e->set("EmitCount", Value::integer(1000));
    ps.update(dm, 0.01f, {0, 5, 20}, 100);
    CHECK(ps.count() <= 100);
    // Removed emitter: its particles go.
    dm.destroy(e);
    ps.update(dm, 0.01f, {0, 5, 20});
    CHECK(ps.count() == 0);
}
