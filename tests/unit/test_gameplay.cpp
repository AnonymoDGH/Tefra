#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/runtime/game_runtime.h"
#include "tefra/scripting/script_host.h"
#include "tefra/studio/templates.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::world;

namespace {

bool any_contains(const std::vector<std::string>& lines, const char* needle) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(needle) != std::string::npos; });
}

} // namespace

// The Obby template is a complete game made of C# scripts (one KillBrick script per brick, ModuleScript config,
// checkpoints, HUD). Every script must compile (identical sources share one class: no CS0101), and a character that
// lands on the lava (a kill brick) must die, then respawn.
TEST_CASE("gameplay: the obby template compiles and a character dropped onto the lava dies and respawns") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    REQUIRE(studio::build_template("obby", dm));

    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    runtime::RuntimeConfig cfg;
    cfg.scripts = &host;
    runtime::GameRuntime game(dm, cfg);
    REQUIRE(game.start());
    const f32 dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) game.step(dt, InputState{});
    CHECK_FALSE(any_contains(logs, "error CS"));
    CHECK_FALSE(any_contains(logs, "exception in"));

    Instance* lava = dm.find_path("Workspace.Lava");
    REQUIRE(lava != nullptr);
    REQUIRE(game.character() != nullptr);
    const u64 first_character = game.character()->id();
    Instance* root = game.character()->find_first_child("HumanoidRootPart");
    REQUIRE(root != nullptr);

    // Teleport the character above the lava (the lava's top is 0.5 m above its centre, 10 m below the drop point).
    root->set("Position", Value::vector3(vec3{0.0f, -6.0f, -110.0f}));
    bool dead = false;
    for (int i = 0; i < 600 && !(dead && game.character() && game.character()->id() != first_character); ++i) {
        game.step(dt, InputState{});
        if (Instance* hum = game.humanoid()) dead = dead || hum->get_float("Health", 100.0f) <= 0.0f;
    }
    CHECK(dead);
    REQUIRE(game.character() != nullptr);
    CHECK(game.character()->id() != first_character); // respawned at a new character
    game.stop();
    host.stop();
}

// A ModuleScript's static state is shared by every script of the session (one module assembly, not copies).
TEST_CASE("gameplay: ModuleScript statics are shared by scripts") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    Instance* rs = dm.service("ReplicatedStorage");
    REQUIRE(rs != nullptr);
    Instance* module = dm.create("ModuleScript", rs, "Shared");
    REQUIRE(module != nullptr);
    module->set("Source", Value::string("public static class Shared { public static int Count; }"));
    // Both scripts have the same class name in different sources: each source is its own namespace, so they coexist.
    const char* sources[] = {
        "public class Counter : TefraScript { public override void Start() { Shared.Count++; } }",
        "public class Counter : TefraScript { public override void Start() { Shared.Count++; Debug.Log(\"count \" + "
        "Shared.Count); } }",
    };
    for (const char* src : sources) {
        Instance* s = dm.create("Script", dm.workspace(), "Counter");
        REQUIRE(s != nullptr);
        s->set("Source", Value::string(src));
    }
    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(dm));
    host.step(1.0f / 60.0f);
    host.stop();
    CHECK_FALSE(any_contains(logs, "error CS"));
    CHECK_FALSE(any_contains(logs, "exception in"));
    CHECK(any_contains(logs, "count 2")); // both Start() calls ran before the log: the static is shared
}

// An anchored part that a script moves is kinematic: the character standing on it is carried along (moving platform).
TEST_CASE("gameplay: a character standing on a script-moved platform is carried by it") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    Instance* platform = dm.create("SpawnLocation", dm.workspace(), "Platform");
    REQUIRE(platform != nullptr);
    platform->set("Position", Value::vector3(vec3{0.0f, 0.0f, 0.0f}));
    platform->set("Size", Value::vector3(vec3{8.0f, 1.0f, 8.0f}));
    platform->set("Anchored", Value::boolean(true));
    Instance* mover = dm.create("Script", platform, "Mover");
    REQUIRE(mover != nullptr);
    mover->set("Source",
               Value::string("public class Mover : TefraScript { float t; public override void Update(float dt) {"
                             " t += dt; ((BasePart)Parent).Position = new Vector3(t * 2f, 0, 0); } }"));

    runtime::RuntimeConfig cfg;
    cfg.scripts = &host;
    runtime::GameRuntime game(dm, cfg);
    REQUIRE(game.start());
    const f32 dt = 1.0f / 60.0f;
    for (int i = 0; i < 90; ++i) game.step(dt, InputState{}); // 1.5 s: the platform moves 3 m along +X
    REQUIRE(game.character() != nullptr);
    Instance* root = game.character()->find_first_child("HumanoidRootPart");
    REQUIRE(root != nullptr);
    CHECK(platform->get_vec3("Position").x == doctest::Approx(3.0f).epsilon(0.05));
    // The character spawned at x = 0 and does not walk: it can only be near x = 3 if the platform carried it.
    CHECK(root->get_vec3("Position").x > 2.0f);
    game.stop();
    host.stop();
}

// A script that is destroyed during Play stops at once: its Update no longer runs.
TEST_CASE("gameplay: a script destroyed during Play stops running") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    Instance* ticker = dm.create("Script", dm.workspace(), "Ticker");
    REQUIRE(ticker != nullptr);
    ticker->set("Source",
                Value::string("public class Ticker : TefraScript { int n; public override void Update(float dt) {"
                              " n++; Debug.Log(\"tick \" + n); if (n == 3) Script.Destroy(); } }"));
    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(dm));
    for (int i = 0; i < 10; ++i) host.step(1.0f / 60.0f);
    host.stop();
    CHECK(any_contains(logs, "tick 3"));
    CHECK_FALSE(any_contains(logs, "tick 4"));
    CHECK(dm.find_path("Workspace.Ticker") == nullptr);
}

// A destroyed script keeps none of what it subscribed: its Heartbeat and InputBegan handlers stop, and a Task.Wait it
// started never resumes its code (the regression: the handlers stayed in the static events and ran after Destroy).
TEST_CASE("gameplay: a destroyed script's handlers and pending waits never run") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    Instance* ghost = dm.create("Script", dm.workspace(), "Ghost");
    REQUIRE(ghost != nullptr);
    ghost->set("Source",
               Value::string("public class Ghost : TefraScript { int n; public override void Start() {"
                             " RunService.Heartbeat += dt => Debug.Log(\"ghost heartbeat\");"
                             " UserInputService.InputBegan += io => Debug.Log(\"ghost input\");"
                             " Wait(); }"
                             " async void Wait() { await Task.Wait(0.05f); Debug.Log(\"ghost resumed\"); }"
                             " public override void Update(float dt) { n++; if (n == 2) Script.Destroy(); } }"));
    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    REQUIRE(host.start(dm));
    const f32 dt = 1.0f / 60.0f;
    for (int i = 0; i < 2; ++i) host.step(dt); // destroyed during the second step
    dm.fire_event(dm.root(), "InputBegan", nullptr, 0.0, "E");
    for (int i = 0; i < 10; ++i) host.step(dt);
    host.stop();
    const auto count = [&logs](const char* needle) {
        return std::count_if(logs.begin(), logs.end(),
                             [&](const std::string& l) { return l.find(needle) != std::string::npos; });
    };
    CHECK(count("ghost heartbeat") == 2);
    CHECK(count("ghost input") == 0);
    CHECK(count("ghost resumed") == 0);
}

// `Workspace.Raycast` works inside a TefraScript, also from Start (before the first physics step): the floor is hit.
TEST_CASE("gameplay: Workspace.Raycast from a script's Start hits the floor") {
    scripting::ScriptHost host;
    std::string err;
    if (!host.init(&err)) {
        MESSAGE("scripting unavailable, skipped: " << err);
        return;
    }
    DataModel dm;
    Instance* floor = dm.create("Part", dm.workspace(), "Floor");
    REQUIRE(floor != nullptr);
    floor->set("Anchored", Value::boolean(true));
    floor->set("Position", Value::vector3(vec3{0.0f, -0.5f, 0.0f}));
    floor->set("Size", Value::vector3(vec3{40.0f, 1.0f, 40.0f}));
    Instance* ray = dm.create("Script", dm.workspace(), "RayScript");
    REQUIRE(ray != nullptr);
    ray->set("Source",
             Value::string("public class RayTest : TefraScript { public override void Start() {"
                           " var hit = Workspace.Raycast(new Vector3(0, 10, 0), new Vector3(0, -1, 0));"
                           " Debug.Log(hit == null ? \"ray miss\" : \"ray hit \" + hit.Instance.Name + \" \" +"
                           " hit.Position.Y.ToString(\"0.0\")); } }"));
    std::vector<std::string> logs;
    host.set_log_callback([&logs](LogLevel, std::string_view message) { logs.emplace_back(message); });
    runtime::RuntimeConfig cfg;
    cfg.scripts = &host;
    cfg.spawn_character = false;
    runtime::GameRuntime game(dm, cfg);
    REQUIRE(game.start());
    game.stop();
    host.stop();
    CHECK_FALSE(any_contains(logs, "error CS"));
    CHECK(any_contains(logs, "ray hit Floor"));
}
