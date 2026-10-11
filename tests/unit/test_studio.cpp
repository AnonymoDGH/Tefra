#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/studio/session.h"

using namespace tefra;
using namespace tefra::studio;
using world::Value;

TEST_CASE("studio session: create, property, delete, undo/redo") {
    Session s;
    world::DataModel& m = s.model();
    world::Instance* p = s.create("Part", m.workspace(), "Box");
    REQUIRE(p);
    const u64 id = p->id();
    CHECK(s.set_property(p, p->cls().find_prop("Position")->index, Value::vector3({1, 2, 3})));
    CHECK(s.undo());
    CHECK(m.find(id)->get_vec3("Position").x == doctest::Approx(0.0f));
    CHECK(s.redo());
    CHECK(m.find(id)->get_vec3("Position").z == doctest::Approx(3.0f));
    s.select(id);
    CHECK(s.destroy_selection());
    CHECK(m.find(id) == nullptr);
    CHECK(s.undo()); // the part comes back with the same id and its property
    REQUIRE(m.find(id));
    CHECK(m.find(id)->get_vec3("Position").y == doctest::Approx(2.0f));
    CHECK(s.undo()); // property
    CHECK(s.undo()); // creation
    CHECK(m.find(id) == nullptr);
}

TEST_CASE("studio session: gesture merges into one undo step; group and duplicate") {
    Session s;
    world::DataModel& m = s.model();
    world::Instance* a = s.create("Part", m.workspace(), "A");
    world::Instance* b = s.create("Part", m.workspace(), "B");
    const u32 pos = a->cls().find_prop("Position")->index;
    s.begin_edit("Move");
    for (int i = 1; i <= 10; ++i) s.set_live(a, pos, Value::vector3({f32(i), 0, 0}));
    s.end_edit();
    const usize depth = s.history().undo_depth();
    CHECK(s.undo());
    CHECK(a->get_vec3("Position").x == doctest::Approx(0.0f));
    CHECK(s.history().undo_depth() == depth - 1);
    s.select_only({a->id(), b->id()});
    world::Instance* g = s.group_selection();
    REQUIRE(g);
    CHECK(a->parent() == g);
    CHECK(b->parent() == g);
    CHECK(s.undo());
    CHECK(a->parent() == m.workspace());
    s.select(a->id());
    const auto dup = s.duplicate_selection();
    REQUIRE(dup.size() == 1);
    CHECK(m.find(dup[0])->name() == "A");
}

TEST_CASE("studio session: play/stop restores the place and runs hooks") {
    Session s;
    world::Instance* p = s.create("Part", s.model().workspace(), "Mover");
    const u64 id = p->id();
    int started = 0, steps = 0, stopped = 0;
    PlayHooks h;
    h.name = "test";
    h.start = [&](world::DataModel&) {
        ++started;
        return true;
    };
    h.step = [&](world::DataModel& m, f32) {
        ++steps;
        m.find(id)->set("Position", Value::vector3({0, f32(steps), 0}));
    };
    h.stop = [&] { ++stopped; };
    s.add_hooks(h);
    const u64 gen = s.generation();
    s.play();
    for (int i = 0; i < 5; ++i) s.step(1.0f / 60.0f);
    CHECK(s.model().find(id)->get_vec3("Position").y == doctest::Approx(5.0f));
    s.stop();
    CHECK(started == 1);
    CHECK(steps == 5);
    CHECK(stopped == 1);
    CHECK(s.generation() != gen);
    REQUIRE(s.model().find(id));
    CHECK(s.model().find(id)->get_vec3("Position").y == doctest::Approx(2.5f)); // inserted default
}
