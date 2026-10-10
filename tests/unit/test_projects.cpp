#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/fs.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/studio/projects.h"
#include "tefra/studio/session.h"
#include "tefra/studio/templates.h"

#include <cstdio>

using namespace tefra;
using namespace tefra::studio;

namespace {
std::string temp_root(const char* name) {
    const std::string root = path_join(path_join(exe_dir(), "test_tmp"), name);
    remove_tree(root);
    make_dirs(root.c_str());
    return root;
}
} // namespace

TEST_CASE("projects: create, recent, rename, duplicate, archive, delete") {
    const std::string root = temp_root("projects");
    ProjectManager pm(path_join(root, "Tefra"), path_join(root, "config"));
    const std::string a = pm.create_project("Obby");
    const std::string b = pm.create_project("Obby");
    CHECK(path_stem(a) == "Obby");
    CHECK(path_stem(b) == "Obby (2)");
    world::DataModel dm;
    REQUIRE(build_template("baseplate", dm));
    REQUIRE(dm.save(a.c_str()));
    REQUIRE(dm.save(b.c_str()));
    pm.touch_recent(a);
    pm.touch_recent(b);
    auto recent = pm.recent();
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].path == b); // most recent first
    CHECK(pm.projects().size() == 2);
    CHECK(pm.thumbnail_for(a) == path_join(path_parent(a), "thumbnail.png"));

    std::string renamed, err;
    REQUIRE(pm.rename(pm.info_for(a), "My: Game?", &renamed, &err));
    CHECK(path_stem(renamed) == "My Game");
    CHECK(file_exists(renamed.c_str()));
    CHECK(pm.recent().size() == 2);
    CHECK_FALSE(pm.rename(pm.info_for(b), "My Game", nullptr, &err)); // name taken

    std::string copy;
    REQUIRE(pm.duplicate(pm.info_for(renamed), &copy, &err));
    CHECK(path_stem(copy) == "My Game copy");
    CHECK(pm.projects().size() == 3);

    REQUIRE(pm.set_archived(pm.info_for(copy), true, &err));
    CHECK(pm.projects().size() == 2);
    REQUIRE(pm.archived().size() == 1);
    CHECK(pm.archived()[0].archived);
    REQUIRE(pm.set_archived(pm.archived()[0], false, &err));
    CHECK(pm.archived().empty());

    REQUIRE(pm.remove(pm.info_for(b), &err));
    CHECK_FALSE(file_exists(b.c_str()));
    CHECK(pm.projects().size() == 2);
    for (const ProjectInfo& p : pm.recent()) CHECK(p.path != b);

    // Loose files keep their thumbnail in the config folder.
    const std::string loose = path_join(root, "loose.tefra");
    REQUIRE(dm.save(loose.c_str()));
    CHECK(path_parent(pm.thumbnail_for(loose)) == path_join(path_join(root, "config"), "thumbs"));
    CHECK(ProjectManager::sanitize_name("  ..") == "Untitled");
    CHECK(format_age(100, 100 + 30) == "just now");
    CHECK(format_age(100, 100 + 7200) == "2 hours ago");
}

TEST_CASE("templates: every template builds and round-trips through .tefra") {
    const std::string root = temp_root("templates");
    for (const TemplateInfo& t : templates()) {
        CAPTURE(t.id);
        world::DataModel dm;
        REQUIRE(build_template(t.id, dm));
        CHECK(dm.workspace()->children().size() > 0);
        const std::string path = path_join(root, std::string(t.id) + ".tefra");
        REQUIRE(dm.save(path.c_str()));
        world::DataModel back;
        std::string err;
        REQUIRE(back.load(path.c_str(), &err));
        CHECK(back.root().descendants().size() == dm.root().descendants().size());
    }
    world::DataModel bad;
    CHECK_FALSE(build_template("nope", bad));
}

TEST_CASE("obby: checkpoints, kill bricks, HUD and scripts are in place") {
    world::DataModel dm;
    REQUIRE(build_template("obby", dm));
    int checkpoints = 0, kills = 0, scripts = 0;
    for (world::Instance* i : dm.root().descendants()) {
        if (i->is_a("SpawnLocation") && i->name().rfind("Checkpoint", 0) == 0) {
            ++checkpoints;
            CHECK_FALSE(i->get_bool("Enabled", true)); // only the start spawn is a spawn point
        }
        if (i->is_a("Script") && i->name() == "KillBrick") ++kills;
        if (i->is_a("BaseScript") || i->is_a("ModuleScript")) ++scripts;
    }
    CHECK(checkpoints == 3);
    CHECK(kills >= 6);
    CHECK(scripts >= 15);
    CHECK(dm.find_path("StarterGui.Hud.TopBar.Timer"));
    CHECK(dm.find_path("StarterGui.Hud.Hud"));
    CHECK(dm.find_path("ReplicatedStorage.ObbyConfig"));
}

TEST_CASE("runtime: player, PlayerGui clone, death below the world and respawn") {
    world::DataModel dm;
    REQUIRE(build_template("obby", dm));
    runtime::RuntimeConfig rc;
    runtime::GameRuntime rt(dm, rc);
    REQUIRE(rt.start());
    REQUIRE(rt.local_player());
    REQUIRE(rt.player_gui());
    CHECK(rt.player_gui()->find_first_child("Hud"));
    REQUIRE(rt.character());
    const u64 first = rt.character()->id();
    InputState in;
    for (int i = 0; i < 30; ++i) rt.step(1.0f / 60.0f, in);
    // Teleport the character under the kill height: it dies, then respawns after Players.RespawnTime (1.5 s).
    world::Instance* root = rt.character()->find_first_child("HumanoidRootPart");
    REQUIRE(root);
    int died = 0;
    world::WorldListener l;
    l.event = [&](const world::GameEvent& e) {
        if (e.name == "Died") ++died;
    };
    dm.add_listener(l);
    root->set("Position", world::Value::vector3({0, -40, 0}));
    for (int i = 0; i < 6; ++i) rt.step(1.0f / 60.0f, in);
    CHECK(died == 1);
    for (int i = 0; i < 120; ++i) rt.step(1.0f / 60.0f, in);
    REQUIRE(rt.character());
    CHECK(rt.character()->id() != first);
    rt.stop();
    CHECK_FALSE(rt.running());
}
