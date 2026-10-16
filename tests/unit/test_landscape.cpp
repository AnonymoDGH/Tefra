#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/fs.h"
#include "tefra/physics/physics_world.h"
#include "tefra/renderer/terrain_field.h"
#include "tefra/world/datamodel.h"
#include "tefra/world/environment_sync.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::world;

namespace {

// A Workspace with one Terrain of the given Kind (enum index: 0 None, 1 Valley, 2 Coast).
Instance* add_terrain(DataModel& dm, u32 kind, f32 sea = 0.0f) {
    Instance* t = dm.create("Terrain", dm.workspace(), "Terrain");
    t->set("Kind", Value::enumeration(kind));
    t->set("SeaLevel", Value::number(sea));
    return t;
}

} // namespace

TEST_CASE("landscape: the field's samples are its heights and bilinear lies between neighbours") {
    render::TerrainDesc d;
    d.resolution = 256;
    d.texel_size = 8.0f; // 2 km, like the valley, at a coarser grid
    d.seed = 1337;
    render::TerrainField f(d);
    const vec2 o = f.sample_origin();
    const u32 n = f.resolution();
    const f32 texel = f.texel_size();
    bool samples_match = true;
    for (u32 z = 0; z < n; z += 17)
        for (u32 x = 0; x < n; x += 19) {
            const f32 h = f.heights()[z * n + x];
            if (std::fabs(f.height_at(o.x + f32(x) * texel, o.y + f32(z) * texel) - h) > 1e-3f) samples_match = false;
        }
    CHECK(samples_match);

    // Halfway between two samples the height is between them (bilinear, no overshoot).
    const u32 x = 100, z = 120;
    const f32 a = f.heights()[z * n + x], b = f.heights()[z * n + x + 1];
    const f32 mid = f.height_at(o.x + (f32(x) + 0.5f) * texel, o.y + f32(z) * texel);
    CHECK(mid >= std::fmin(a, b) - 1e-4f);
    CHECK(mid <= std::fmax(a, b) + 1e-4f);
}

TEST_CASE("landscape: terrain_height of a Terrain instance equals the shared field everywhere") {
    DataModel dm;
    Instance* t = add_terrain(dm, 1); // Valley, default seed
    render::TerrainDesc desc;         // the same defaults terrain_desc() gives a Valley
    const auto field = render::TerrainField::acquire(desc);
    bool same = true;
    for (f32 z = -900.0f; z <= 900.0f; z += 230.0f)
        for (f32 x = -900.0f; x <= 900.0f; x += 230.0f)
            if (terrain_height(*t, x, z) != field->height_at(x, z)) same = false;
    CHECK(same);
    CHECK(terrain_height(*t, 0.0f, 0.0f) == field->height_at(0.0f, 0.0f));
}

TEST_CASE("landscape: a Terrain with Kind None is no landscape") {
    DataModel dm;
    Instance* none = add_terrain(dm, 0);
    render::TerrainDesc d;
    CHECK_FALSE(terrain_desc(*none, d));
    CHECK(find_terrain(dm) == nullptr);
    CHECK(terrain_height(*none, 10.0f, 10.0f) == 0.0f);

    Instance* coast = add_terrain(dm, 2, 0.5f);
    CHECK(find_terrain(dm) == coast);
    CHECK(terrain_desc(*coast, d));
    CHECK(d.kind == render::TerrainKind::Coast);
    CHECK(d.sea_level == doctest::Approx(0.5f));
    CHECK(d.seed == 1337u);
}

TEST_CASE("landscape: find_ocean returns the Workspace's Ocean") {
    DataModel dm;
    CHECK(find_ocean(dm) == nullptr);
    Instance* o = dm.create("Ocean", dm.workspace(), "Ocean");
    CHECK(find_ocean(dm) == o);
}

TEST_CASE("physics: the landscape heightfield is raycast-able and reports its Terrain") {
    DataModel dm;
    physics::PhysicsWorld pw{dm};
    Instance* terrain = add_terrain(dm, 1);

    // A plane that rises 0.25 m per metre along +X: height(x, z) = 0.25 x on a 64 x 64 grid of 1 m cells.
    constexpr u32 n = 64;
    std::vector<f32> heights(n * n);
    for (u32 z = 0; z < n; ++z)
        for (u32 x = 0; x < n; ++x) heights[z * n + x] = 0.25f * f32(x);
    CHECK_FALSE(pw.has_heightfield());
    pw.set_heightfield(heights.data(), n, 1.0f, vec2{0.0f, 0.0f}, terrain);
    CHECK(pw.has_heightfield());

    physics::RaycastResult hit;
    CHECK(pw.raycast(vec3{10.0f, 50.0f, 20.0f}, vec3{0.0f, -1.0f, 0.0f}, 100.0f, hit));
    CHECK(hit.instance == terrain);
    CHECK(hit.position.y == doctest::Approx(2.5f).epsilon(0.01));
    CHECK(hit.normal.y > 0.9f);

    // Replacing the field moves the surface (+10 m); clearing it removes the body.
    for (f32& h : heights) h += 10.0f;
    pw.set_heightfield(heights.data(), n, 1.0f, vec2{0.0f, 0.0f}, terrain);
    CHECK(pw.raycast(vec3{10.0f, 50.0f, 20.0f}, vec3{0.0f, -1.0f, 0.0f}, 100.0f, hit));
    CHECK(hit.position.y == doctest::Approx(12.5f).epsilon(0.01));
    pw.clear_heightfield();
    CHECK(pw.raycast(vec3{10.0f, 50.0f, 20.0f}, vec3{0.0f, -1.0f, 0.0f}, 100.0f, hit) == false);
    CHECK_FALSE(pw.has_heightfield());
}

TEST_CASE("terrain field: the disk cache returns the generated samples exactly") {
    // TEFRA_HOME points the configuration folder (and so the field cache) at a scratch directory of this test.
    const std::string home = "tefra_field_cache_test";
    remove_tree(home);
#if defined(_WIN32)
    _putenv_s("TEFRA_HOME", home.c_str());
#else
    setenv("TEFRA_HOME", home.c_str(), 1);
#endif
    render::TerrainDesc d;
    d.resolution = 256;
    d.texel_size = 4.0f;
    d.seed = 77;
    d.kind = render::TerrainKind::Coast;
    d.sea_level = 0.5f;
    const render::TerrainField generated(d); // writes the cache
    CHECK(is_dir(path_join(home, ".config/cache/terrain")));
    const render::TerrainField cached(d); // reads it back
    CHECK(cached.heights() == generated.heights());
    CHECK(cached.mask() == generated.mask());
    CHECK(cached.shore() == generated.shore());
    CHECK(cached.min_height() == generated.min_height());
    CHECK(cached.max_height() == generated.max_height());
    CHECK(cached.desc().sea_level == generated.desc().sea_level);
    // A sea-level drag writes a field per value: only the most recent few stay on disk.
    for (int i = 0; i < 8; ++i) {
        render::TerrainDesc sea = d;
        sea.sea_level = 1.0f + 0.25f * f32(i);
        const render::TerrainField f(sea);
        (void)f;
    }
    std::vector<DirEntry> files;
    list_dir(path_join(home, ".config/cache/terrain"), files);
    usize fields = 0;
    for (const DirEntry& e : files) fields += e.name.size() > 5 && e.name.substr(e.name.size() - 5) == ".tfld" ? 1 : 0;
    CHECK(fields <= 6);
    CHECK(fields >= 1);
    // Back to the real configuration folder for the tests that follow (an empty TEFRA_HOME counts as unset).
#if defined(_WIN32)
    _putenv_s("TEFRA_HOME", "");
#else
    unsetenv("TEFRA_HOME");
#endif
    remove_tree(home);
}
