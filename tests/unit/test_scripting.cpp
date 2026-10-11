#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/scripting/script_host.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::world;

#ifndef TEFRA_SCRIPTING_FIXTURES
#define TEFRA_SCRIPTING_FIXTURES "tests/scripting/fixtures"
#endif

namespace {

std::string fixture(const char* name) { return std::string(TEFRA_SCRIPTING_FIXTURES) + "/" + name; }

bool contains(const std::vector<std::string>& lines, const std::string& needle) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(needle) != std::string::npos; });
}

void run_frames(scripting::ScriptHost& host, int frames) {
    for (int i = 0; i < frames; ++i) host.step(1.0f / 60.0f);
}

} // namespace

TEST_CASE("scripting: a missing managed assembly is reported and the host stays unavailable") {
    scripting::ScriptHost host;
    std::string err;
    if (host.init(&err, "/nonexistent/tefra/managed")) {
        // An earlier test in this process already started the runtime; the check cannot run again.
        MESSAGE("runtime already started, missing-assembly check skipped");
        return;
    }
    CHECK_FALSE(err.empty());
    CHECK_FALSE(host.available());
    DataModel model;
    CHECK_FALSE(host.start(model));
    host.step(0.016f); // no-ops when not running
    host.stop();
}

TEST_CASE("scripting: an empty place starts and steps without scripts") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel model;
    REQUIRE(host.start(model));
    CHECK(host.running());
    run_frames(host, 3);
    host.stop();
    CHECK_FALSE(host.running());
}

TEST_CASE("scripting: spinner rotates, spawner creates parts, Task.Wait resumes on the loop") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel model;
    REQUIRE(model.load(fixture("spin.tefra").c_str(), &err));

    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(model));
    run_frames(host, 60); // one second of play
    host.stop();

    Instance* spinner = model.find_path("Workspace.Spinner");
    REQUIRE(spinner != nullptr);
    CHECK(spinner->get_vec3("Orientation").y == doctest::Approx(90.0f).epsilon(1e-3));

    int spawned = 0;
    for (Instance* child : model.workspace()->children())
        if (child->name().rfind("Spawned", 0) == 0) ++spawned;
    CHECK(spawned == 10);
    Instance* third = model.find_path("Workspace.Spawned3");
    REQUIRE(third != nullptr);
    CHECK(third->get_vec3("Position").x == doctest::Approx(9.0f));
    CHECK(third->get_bool("Anchored"));

    // Task.Wait(0.5) resumed after half a second and renamed the lamp.
    CHECK(model.find_path("Workspace.Lamp_ready") != nullptr);
    CHECK(contains(logs, "spawned\t10"));
    CHECK(contains(logs, "waited"));
}

TEST_CASE("scripting: Changed and Destroying events are delivered from the engine loop") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel model;
    REQUIRE(model.load(fixture("events.tefra").c_str(), &err));

    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(model));
    run_frames(host, 2);
    host.stop();

    // Changed fires for the Position the script set in Start; Destroying fires for a descendant of a destroyed folder.
    CHECK(contains(logs, "moved\tWatched"));
    CHECK(contains(logs, "inner destroying"));
    CHECK(model.find_path("Workspace.Box") == nullptr);
}

TEST_CASE("scripting: CFrame round trip, Model PivotTo and the value types run in a script") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel model;
    REQUIRE(model.load(fixture("mathtypes.tefra").c_str(), &err));

    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(model));
    run_frames(host, 2);
    host.stop();

    // The script sets part.CFrame from fromEulerAnglesYXZ(0.3, 0.5, 0.2) and reads it back: the Orientation stored in
    // the DataModel is those angles in degrees (Y-X-Z convention shared with orientation_to_quat).
    Instance* probe = model.find_path("Workspace.Probe");
    REQUIRE(probe != nullptr);
    const auto orientation = probe->get_vec3("Orientation");
    CHECK(orientation.x == doctest::Approx(17.1887f).epsilon(1e-3));
    CHECK(orientation.y == doctest::Approx(28.6479f).epsilon(1e-3));
    CHECK(orientation.z == doctest::Approx(11.4592f).epsilon(1e-3));

    // Model.PivotTo moved the whole model rigidly: A went to the pivot, B followed the same rotation and offset.
    Instance* partB = model.find_path("Workspace.Rig.B");
    REQUIRE(partB != nullptr);
    const auto b = partB->get_vec3("Position");
    CHECK(b.x == doctest::Approx(10.0f).epsilon(1e-4));
    CHECK(b.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(b.z == doctest::Approx(-2.0f).epsilon(1e-4));

    CHECK(contains(logs, "roundtrip ok"));
    CHECK(contains(logs, "inverse ok"));
    CHECK(contains(logs, "random seeded ok"));
    CHECK(contains(logs, "udim2 0.750 15.000 0.500 20.000"));
}

TEST_CASE("scripting: compile errors and exceptions disable only the script that failed") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel model;
    REQUIRE(model.load(fixture("broken.tefra").c_str(), &err));

    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(model));
    run_frames(host, 10);
    host.stop();

    // The syntax error names the script and its position; the other scripts keep running.
    CHECK(contains(logs, "Workspace.Bad.Broken(3,17): error CS1525"));
    // The exception stops the script after its third Update, with a stack trace.
    CHECK(contains(logs, "Workspace.Dummy.Thrower: exception in Update"));
    CHECK(contains(logs, "boom"));
    Instance* dummy = model.find_path("Workspace.Dummy");
    REQUIRE(dummy != nullptr);
    CHECK(dummy->get_vec3("Position").x == doctest::Approx(3.0f));
    Instance* spinner = model.find_path("Workspace.Spinner");
    REQUIRE(spinner != nullptr);
    CHECK(spinner->get_vec3("Orientation").y == doctest::Approx(15.0f).epsilon(1e-3)); // 10 frames at 90 deg/s
}
