#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/json_value.h"
#include "tefra/world/datamodel.h"

using namespace tefra;
using namespace tefra::world;

TEST_CASE("json value: parse, access, dump round trip") {
    JsonValue v;
    std::string err;
    REQUIRE(JsonValue::parse(R"({"a":1,"b":[true,null,"x\né"],"c":{"d":-2.5e1}})", v, &err));
    CHECK(v["a"].as_int() == 1);
    CHECK(v["b"].size() == 3);
    CHECK(v["b"].at(0).as_bool());
    CHECK(v["b"].at(1).is_null());
    CHECK(v["b"].at(2).as_string() == "x\n\xc3\xa9");
    CHECK(v["c"]["d"].as_number() == doctest::Approx(-25.0));
    CHECK(v["missing"].is_null());
    JsonValue back;
    REQUIRE(JsonValue::parse(v.dump(true), back));
    CHECK(back.dump() == v.dump());
    CHECK_FALSE(JsonValue::parse("{\"a\":}", v, &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(JsonValue::parse("[1,2] x", v));
}

TEST_CASE("datamodel: services, create, properties, paths") {
    DataModel dm;
    REQUIRE(dm.workspace() != nullptr);
    REQUIRE(dm.service("Lighting") != nullptr);
    CHECK(dm.create("Workspace") == nullptr); // services are not creatable
    CHECK(dm.create("NoSuchClass") == nullptr);
    Instance* model = dm.create("Model", dm.workspace(), "House");
    Instance* part = dm.create("Part", model, "Door");
    REQUIRE(part);
    CHECK(part->is_a("BasePart"));
    CHECK(part->is_a("Instance"));
    CHECK_FALSE(part->is_a("Model"));
    CHECK(part->get_vec3("Size").x == doctest::Approx(4.0f));
    CHECK(part->set("Position", Value::vector3({1, 2, 3})));
    CHECK_FALSE(part->set("Position", Value::number(3.0))); // wrong type
    CHECK_FALSE(part->set("Nope", Value::number(3.0)));
    CHECK(part->set("Material", Value::enumeration(4)));
    CHECK_FALSE(part->set("Material", Value::enumeration(999)));
    CHECK(dm.find_path("Workspace.House.Door") == part);
    CHECK(dm.find_path("game.Workspace.House.Door") == part);
    CHECK(part->full_name() == "Workspace.House.Door");
    CHECK(model->set("PrimaryPart", Value::reference(part->id())));
}

TEST_CASE("datamodel: listeners, reparent, destroy clears references") {
    DataModel dm;
    int added = 0, removed = 0, changed = 0;
    WorldListener l;
    l.added = [&](Instance&) { ++added; };
    l.removing = [&](Instance&) { ++removed; };
    l.changed = [&](Instance&, u32) { ++changed; };
    dm.add_listener(l);
    Instance* model = dm.create("Model", dm.workspace());
    Instance* part = dm.create("Part", model);
    CHECK(added == 2);
    part->set("Anchored", Value::boolean(true));
    part->set("Anchored", Value::boolean(true)); // no change, no event
    CHECK(changed == 1);
    model->set("PrimaryPart", Value::reference(part->id()));
    CHECK_FALSE(dm.set_parent(model, part)); // cycles are refused
    Instance* folder = dm.create("Folder", dm.workspace());
    CHECK(dm.set_parent(part, folder));
    CHECK(part->parent() == folder);
    CHECK(dm.destroy(part));
    CHECK(removed == 1);
    CHECK(model->get("PrimaryPart")->ref == 0);
    CHECK_FALSE(dm.destroy(dm.workspace()));
}

TEST_CASE("datamodel: save/load and clone keep the tree and remap references") {
    DataModel a;
    Instance* model = a.create("Model", a.workspace(), "Car");
    Instance* body = a.create("Part", model, "Body");
    body->set("Color", Value::color3({1, 0, 0}));
    body->set("Shape", Value::enumeration(2));
    model->set("PrimaryPart", Value::reference(body->id()));
    Instance* script = a.create("Script", model, "Drive");
    script->set("Source", Value::string("class Drive : Behaviour { }"));
    a.service("Lighting")->set("ClockTime", Value::number(17.5));

    DataModel b;
    std::string err;
    REQUIRE(b.from_json(a.to_json(), &err));
    Instance* car = b.find_path("Workspace.Car");
    REQUIRE(car);
    Instance* b_body = car->find_first_child("Body");
    REQUIRE(b_body);
    CHECK(b_body->get_vec3("Color").x == doctest::Approx(1.0f));
    CHECK(b_body->get("Shape")->as_enum() == 2u);
    CHECK(car->get("PrimaryPart")->ref == b_body->id());
    CHECK(car->find_first_child("Drive")->get_string("Source") == "class Drive : Behaviour { }");
    CHECK(b.service("Lighting")->get_float("ClockTime") == doctest::Approx(17.5f));

    Instance* copy = b.clone(*car, b.workspace());
    REQUIRE(copy);
    CHECK(copy->id() != car->id());
    Instance* copy_body = copy->find_first_child("Body");
    CHECK(copy->get("PrimaryPart")->ref == copy_body->id());
}

TEST_CASE("orientation: quaternion round trip") {
    const vec3 deg{20.0f, -35.0f, 10.0f};
    const vec3 back = quat_to_orientation(orientation_to_quat(deg));
    CHECK(back.x == doctest::Approx(deg.x).epsilon(1e-3));
    CHECK(back.y == doctest::Approx(deg.y).epsilon(1e-3));
    CHECK(back.z == doctest::Approx(deg.z).epsilon(1e-3));
}
