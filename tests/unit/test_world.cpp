#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/json_value.h"
#include "tefra/world/datamodel.h"

#include <string>
#include <vector>

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

TEST_CASE("attributes: set, get, remove and change events") {
    DataModel dm;
    Instance* part = dm.create("Part", dm.workspace(), "Crate");
    REQUIRE(part);
    std::vector<std::string> changed;
    WorldListener l;
    l.event = [&](const GameEvent& e) {
        if (e.name == "AttributeChanged") changed.emplace_back(e.text);
    };
    dm.add_listener(l);

    CHECK(part->set_attribute("Health", Value::integer(100)));
    CHECK(part->get_attribute("Health")->type == PropType::Int);
    CHECK(part->get_attribute("Health")->as_int() == 100);
    const u64 rev = dm.revision();
    CHECK(part->set_attribute("Health", Value::integer(100))); // equal value: accepted, nothing changes
    CHECK(dm.revision() == rev);
    CHECK(part->set_attribute("Speed", Value::number(2.5)));
    CHECK(part->set_attribute("Label", Value::string("crate")));
    CHECK(part->set_attribute("Offset", Value::vector3({1, 2, 3})));
    CHECK(part->set_attribute("Tint", Value::color3({1, 0, 0})));
    CHECK(part->set_attribute("Armed", Value::boolean(true)));
    CHECK(part->get_attribute("Label")->s == "crate");
    CHECK(part->get_attribute("Offset")->as_vec3().y == doctest::Approx(2.0f));
    CHECK(dm.revision() > rev);
    const std::vector<std::string> expected{"Health", "Speed", "Label", "Offset", "Tint", "Armed"};
    CHECK(changed == expected);

    // Other value types and empty names are refused.
    CHECK_FALSE(part->set_attribute("Mode", Value::enumeration(1)));
    CHECK_FALSE(part->set_attribute("Target", Value::reference(part->id())));
    CHECK_FALSE(part->set_attribute("", Value::boolean(true)));
    CHECK(part->get_attribute("Mode") == nullptr);
    CHECK(part->attributes().size() == 6);
    CHECK(part->attributes().begin()->first == "Armed"); // sorted by name

    CHECK(part->remove_attribute("Speed"));
    CHECK_FALSE(part->remove_attribute("Speed"));
    CHECK(part->get_attribute("Speed") == nullptr);
    CHECK(changed.back() == "Speed");

    // Detached instances change without events.
    CHECK(dm.set_parent(part, nullptr));
    const usize events_before = changed.size();
    CHECK(part->set_attribute("Health", Value::integer(5)));
    CHECK(changed.size() == events_before);
}

TEST_CASE("tags: add, remove, query, tagged instances and events") {
    DataModel dm;
    Instance* a = dm.create("Part", dm.workspace(), "A");
    Instance* b = dm.create("Part", dm.workspace(), "B");
    Instance* c = dm.create("Model", dm.workspace(), "C");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    std::vector<std::string> events;
    WorldListener l;
    l.event = [&](const GameEvent& e) {
        if (e.name == "TagAdded" || e.name == "TagRemoved")
            events.push_back(std::string(e.name) + ":" + std::string(e.text));
    };
    dm.add_listener(l);

    CHECK(b->add_tag("Enemy"));
    CHECK_FALSE(b->add_tag("Enemy")); // already tagged
    CHECK_FALSE(b->add_tag(""));
    CHECK(a->add_tag("Enemy"));
    CHECK(a->add_tag("Breakable"));
    CHECK(c->add_tag("Enemy"));
    CHECK(a->has_tag("Enemy"));
    CHECK_FALSE(a->has_tag("enemy")); // case-sensitive
    REQUIRE(a->tags().size() == 2);
    CHECK(*a->tags().begin() == "Breakable"); // sorted

    // Tagged instances come back in id order: a, b, c.
    const std::vector<Instance*> enemies = dm.tagged("Enemy");
    REQUIRE(enemies.size() == 3);
    CHECK(enemies[0] == a);
    CHECK(enemies[1] == b);
    CHECK(enemies[2] == c);
    CHECK(dm.tagged("Nothing").empty());

    CHECK(b->remove_tag("Enemy"));
    CHECK_FALSE(b->remove_tag("Enemy"));
    CHECK(dm.tagged("Enemy").size() == 2);

    // Detached instances are not listed until they are back under the DataModel.
    CHECK(dm.set_parent(a, nullptr));
    CHECK(dm.tagged("Enemy").size() == 1);
    CHECK(dm.set_parent(a, dm.workspace()));
    CHECK(dm.tagged("Enemy").size() == 2);

    // Destroying an instance drops its tags from the index.
    CHECK(dm.destroy(c));
    const std::vector<Instance*> left = dm.tagged("Enemy");
    REQUIRE(left.size() == 1);
    CHECK(left[0] == a);

    REQUIRE(events.size() == 5);
    CHECK(events[0] == "TagAdded:Enemy");
    CHECK(events[2] == "TagAdded:Breakable");
    CHECK(events[4] == "TagRemoved:Enemy");
}

TEST_CASE("attributes and tags: save/load round trip, old files still load") {
    DataModel a;
    Instance* crate = a.create("Part", a.workspace(), "Crate");
    REQUIRE(crate);
    crate->set_attribute("Health", Value::integer(100));
    crate->set_attribute("Speed", Value::number(2.5));
    crate->set_attribute("Label", Value::string("wood crate"));
    crate->set_attribute("Offset", Value::vector3({1, 2, 3}));
    crate->set_attribute("Tint", Value::color3({1, 0.5f, 0}));
    crate->set_attribute("Armed", Value::boolean(false));
    crate->add_tag("Breakable");
    crate->add_tag("Solid");
    a.create("Part", a.workspace(), "Plain");
    const std::string text = a.to_json().dump(true);
    CHECK(text.find("\"attributes\"") != std::string::npos);
    CHECK(text.find("\"tags\"") != std::string::npos);

    DataModel b;
    std::string err;
    JsonValue doc;
    REQUIRE(JsonValue::parse(text, doc, &err));
    REQUIRE(b.from_json(doc, &err));
    Instance* b_crate = b.find_path("Workspace.Crate");
    REQUIRE(b_crate);
    REQUIRE(b_crate->get_attribute("Health"));
    CHECK(b_crate->get_attribute("Health")->type == PropType::Int);
    CHECK(b_crate->get_attribute("Health")->as_int() == 100);
    CHECK(b_crate->get_attribute("Speed")->type == PropType::Float);
    CHECK(b_crate->get_attribute("Speed")->as_float() == doctest::Approx(2.5f));
    CHECK(b_crate->get_attribute("Label")->s == "wood crate");
    CHECK(b_crate->get_attribute("Offset")->type == PropType::Vector3);
    CHECK(b_crate->get_attribute("Offset")->as_vec3().z == doctest::Approx(3.0f));
    CHECK(b_crate->get_attribute("Tint")->type == PropType::Color3);
    CHECK(b_crate->get_attribute("Tint")->n[1] == doctest::Approx(0.5));
    CHECK(b_crate->get_attribute("Armed")->type == PropType::Bool);
    CHECK_FALSE(b_crate->get_attribute("Armed")->as_bool());
    CHECK(b_crate->attributes().size() == 6);
    CHECK(b_crate->has_tag("Solid"));
    CHECK(b.tagged("Breakable").size() == 1); // the tag index is rebuilt on load
    CHECK(b.find_path("Workspace.Plain")->attributes().empty());
    CHECK(b.to_json().dump(true) == text);

    // A file without the keys loads as before; unusable entries are skipped, the rest is kept.
    const char* old_file = R"({"format":"tefra-place","version":1,"services":[{"class":"Workspace","props":{},
        "children":[{"class":"Part","id":50,"props":{"Name":"Old"}},
        {"class":"Part","id":51,"props":{"Name":"Mixed"},
         "attributes":{"Bad":{"x":1},"Good":3},"tags":["Ok",5,""]}]}]})";
    DataModel c;
    REQUIRE(JsonValue::parse(old_file, doc, &err));
    REQUIRE(c.from_json(doc, &err));
    Instance* old = c.find_path("Workspace.Old");
    REQUIRE(old);
    CHECK(old->attributes().empty());
    CHECK(old->tags().empty());
    Instance* mixed = c.find_path("Workspace.Mixed");
    REQUIRE(mixed);
    CHECK(mixed->get_attribute("Good")->as_int() == 3);
    CHECK(mixed->get_attribute("Bad") == nullptr);
    CHECK(mixed->tags().size() == 1);
    CHECK(c.tagged("Ok").size() == 1);
}

TEST_CASE("attributes and tags: clone copies them and indexes the copy") {
    DataModel dm;
    Instance* model = dm.create("Model", dm.workspace(), "Wall");
    Instance* brick = dm.create("Part", model, "Brick");
    REQUIRE(brick);
    brick->set_attribute("Hits", Value::integer(3));
    brick->add_tag("Breakable");
    Instance* copy = dm.clone(*model, dm.workspace());
    REQUIRE(copy);
    Instance* copy_brick = copy->find_first_child("Brick");
    REQUIRE(copy_brick);
    REQUIRE(copy_brick->get_attribute("Hits"));
    CHECK(copy_brick->get_attribute("Hits")->as_int() == 3);
    CHECK(copy_brick->has_tag("Breakable"));
    CHECK(dm.tagged("Breakable").size() == 2);

    // The copy has its own storage.
    copy_brick->remove_attribute("Hits");
    CHECK(brick->get_attribute("Hits") != nullptr);
    CHECK(dm.destroy(copy));
    CHECK(dm.tagged("Breakable").size() == 1);
}

TEST_CASE("orientation: quaternion round trip") {
    const vec3 deg{20.0f, -35.0f, 10.0f};
    const vec3 back = quat_to_orientation(orientation_to_quat(deg));
    CHECK(back.x == doctest::Approx(deg.x).epsilon(1e-3));
    CHECK(back.y == doctest::Approx(deg.y).epsilon(1e-3));
    CHECK(back.z == doctest::Approx(deg.z).epsilon(1e-3));
}
