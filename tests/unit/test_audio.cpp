#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/audio/audio_system.h"
#include "tefra/audio/sound_math.h"
#include "tefra/core/fs.h"
#include "tefra/core/input.h"
#include "tefra/runtime/game_runtime.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace tefra;
using namespace tefra::audio;
using namespace tefra::runtime;
using namespace tefra::world;

namespace {

std::string test_dir() {
    const std::string root = path_join(path_join(exe_dir(), "test_tmp"), "audio");
    make_dirs(root.c_str());
    return root;
}

// A mono 16-bit WAV with a 440 Hz tone, written by hand so the test does not depend on the generator.
std::string write_test_wav(const char* name, u32 rate, f32 seconds) {
    const u32 frames = static_cast<u32>(static_cast<f32>(rate) * seconds);
    std::vector<u8> out;
    auto put32 = [&out](u32 v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>((v >> (8 * i)) & 0xFFu));
    };
    auto put16 = [&out](u32 v) {
        out.push_back(static_cast<u8>(v & 0xFFu));
        out.push_back(static_cast<u8>((v >> 8) & 0xFFu));
    };
    auto tag = [&out](const char* t) { out.insert(out.end(), t, t + 4); };
    tag("RIFF");
    put32(36 + frames * 2);
    tag("WAVE");
    tag("fmt ");
    put32(16);
    put16(1); // PCM
    put16(1); // mono
    put32(rate);
    put32(rate * 2);
    put16(2);  // block align
    put16(16); // bits
    tag("data");
    put32(frames * 2);
    for (u32 i = 0; i < frames; ++i) {
        const f64 v = std::sin(6.283185307 * 440.0 * static_cast<f64>(i) / static_cast<f64>(rate));
        put16(static_cast<u32>(static_cast<u16>(static_cast<i16>(v * 0.5 * 32767.0))));
    }
    const std::string path = path_join(test_dir(), name);
    file_write_all(path.c_str(), out.data(), out.size());
    return path;
}

struct Counts {
    int ended = 0;
    int looped = 0;
};

// A DataModel with one GameRuntime running the audio system on the null backend (deterministic, no device).
struct Session {
    DataModel dm;
    GameRuntime rt;
    AudioSystem* audio = nullptr;
    Counts counts;

    explicit Session(bool spawn_character) : rt(dm, make_config(spawn_character)) {
        auto system = std::make_unique<AudioSystem>(AudioOptions{false});
        audio = system.get();
        rt.add_system(std::move(system));
        WorldListener listener;
        listener.event = [this](const GameEvent& e) {
            if (e.name == "Ended") ++counts.ended;
            if (e.name == "Looped") ++counts.looped;
        };
        (void)dm.add_listener(std::move(listener));
    }

    static RuntimeConfig make_config(bool spawn_character) {
        RuntimeConfig cfg;
        cfg.spawn_character = spawn_character;
        return cfg;
    }

    void run(int frames, f32 dt) {
        const InputState idle{};
        for (int i = 0; i < frames; ++i) rt.step(dt, idle);
    }
};

} // namespace

TEST_CASE("audio: attenuation curve") {
    CHECK(attenuation_gain(1.0f, 3.0f, 120.0f) == doctest::Approx(1.0f)); // inside the min distance
    CHECK(attenuation_gain(3.0f, 3.0f, 120.0f) == doctest::Approx(1.0f));
    CHECK(attenuation_gain(120.0f, 3.0f, 120.0f) == doctest::Approx(0.0f)); // silent from the max distance
    CHECK(attenuation_gain(500.0f, 3.0f, 120.0f) == doctest::Approx(0.0f));

    f32 prev = 1.0f;
    bool monotonic = true;
    for (int d = 3; d <= 120; ++d) {
        const f32 g = attenuation_gain(static_cast<f32>(d), 3.0f, 120.0f);
        if (g > prev + 1e-6f) monotonic = false;
        prev = g;
    }
    CHECK(monotonic);

    // RolloffScale 2 falls off faster, 0.5 slower.
    CHECK(attenuation_gain(30.0f, 3.0f, 120.0f, 2.0f) < attenuation_gain(30.0f, 3.0f, 120.0f, 1.0f));
    CHECK(attenuation_gain(30.0f, 3.0f, 120.0f, 0.5f) > attenuation_gain(30.0f, 3.0f, 120.0f, 1.0f));

    // Linear falloff is a straight line between the two distances.
    CHECK(attenuation_gain(61.5f, 3.0f, 120.0f, 1.0f, Falloff::Linear) == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(attenuation_gain(3.0f, 3.0f, 120.0f, 1.0f, Falloff::Linear) == doctest::Approx(1.0f));

    // Degenerate ranges do not divide by zero.
    CHECK(attenuation_gain(5.0f, 0.0f, 0.0f) == doctest::Approx(0.0f));
    CHECK(attenuation_gain(0.0f, 0.0f, 10.0f) == doctest::Approx(1.0f));
}

TEST_CASE("audio: builtin names and sound id resolution") {
    CHECK(is_builtin_name("checkpoint"));
    CHECK(is_builtin_name("footstep_grass"));
    CHECK_FALSE(is_builtin_name(""));
    CHECK_FALSE(is_builtin_name("../secret"));
    CHECK_FALSE(is_builtin_name("a/b"));
    CHECK_FALSE(is_builtin_name("Jump"));
    CHECK_FALSE(is_builtin_name("jump.wav"));

    CHECK(builtin_sound_path("checkpoint", {}).empty());
    CHECK(builtin_sound_path("no_such_effect", {exe_dir()}).empty());
    CHECK(builtin_sound_path("../checkpoint", {exe_dir()}).empty());
    // The build copies resources/sfx next to the executables; a unit test runs from there.
    const std::string found = builtin_sound_path("checkpoint", {"/no/such/folder", exe_dir()});
    CHECK(found == path_join(path_join(path_join(exe_dir(), "resources"), "sfx"), "checkpoint.wav"));

    bool unsupported = false;
    auto project = [](std::string_view p) { return std::string("/project/") + std::string(p); };
    const std::vector<std::string> dirs = {exe_dir()};
    CHECK(resolve_sound_id("", project, dirs, &unsupported).empty());
    CHECK_FALSE(unsupported);
    CHECK(resolve_sound_id("rbxassetid://123456", project, dirs, &unsupported).empty());
    CHECK(unsupported);
    CHECK(resolve_sound_id("sounds/hit.ogg", project, dirs, &unsupported) == "/project/sounds/hit.ogg");
    CHECK(resolve_sound_id("/abs/hit.wav", project, dirs, &unsupported) == "/abs/hit.wav");
    CHECK(resolve_sound_id("C:/abs/hit.wav", project, dirs, &unsupported) == "C:/abs/hit.wav");
    CHECK(resolve_sound_id("builtin:coin", project, dirs, &unsupported) == builtin_sound_path("coin", dirs));
}

TEST_CASE("audio: built-in effects decode with their expected lengths") {
    struct Expect {
        const char* name;
        f64 seconds;
    };
    const Expect table[] = {{"jump", 0.16},      {"land", 0.16},    {"footstep_grass", 0.2}, {"footstep_stone", 0.12},
                            {"checkpoint", 1.0}, {"win", 1.2},      {"death", 0.5},          {"click", 0.04},
                            {"coin", 0.32},      {"wind_loop", 3.6}};
    Session s(false);
    Instance* snd = s.dm.create("Sound", s.dm.workspace(), "Sfx");
    REQUIRE(snd != nullptr);
    s.rt.start();
    for (const Expect& e : table) {
        snd->set("SoundId", Value::string(std::string("builtin:") + e.name));
        s.run(1, 0.01f);
        INFO(e.name);
        CHECK(snd->get_float("TimeLength") == doctest::Approx(e.seconds).epsilon(0.01));
    }
    s.rt.stop();
}

TEST_CASE("audio: null backend keeps time and fires Ended and Looped") {
    Session s(false);
    const std::string wav = write_test_wav("half.wav", 8000, 0.5f); // 0.5 s
    Instance* snd = s.dm.create("Sound", s.dm.workspace(), "Beep");
    REQUIRE(snd != nullptr);
    snd->set("SoundId", Value::string(wav));
    s.rt.start();
    CHECK(std::string(s.audio->backend_name()) == "null");

    snd->set("Playing", Value::boolean(true));
    s.run(3, 0.1f); // 0.3 s
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.3f).epsilon(0.02));
    CHECK(snd->get_float("TimeLength") == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(s.counts.ended == 0);

    s.run(3, 0.1f); // reaches the end
    CHECK(s.counts.ended == 1);
    CHECK_FALSE(snd->get_bool("Playing"));
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.0f));
    s.run(2, 0.1f);
    CHECK(s.counts.ended == 1); // not playing: no second Ended

    // Looped: wraps once in 0.6 s of a 0.5 s clip.
    snd->set("Looped", Value::boolean(true));
    snd->set("Playing", Value::boolean(true));
    s.run(6, 0.1f);
    CHECK(s.counts.looped == 1);
    CHECK(s.counts.ended == 1);
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.1f).epsilon(0.02));

    // Pausing keeps the position.
    snd->set("Playing", Value::boolean(false));
    s.run(2, 0.1f);
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.1f).epsilon(0.02));

    // PlaybackSpeed 2 advances twice as fast (0.1 + 2 x 0.1).
    snd->set("Looped", Value::boolean(false));
    snd->set("PlaybackSpeed", Value::number(2.0));
    snd->set("Playing", Value::boolean(true));
    s.run(1, 0.1f);
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.3f).epsilon(0.02));

    // A seek written by the game is applied: 0.2 + 0.05 x 2 = 0.3.
    snd->set("PlaybackSpeed", Value::number(1.0));
    snd->set("TimePosition", Value::number(0.2));
    s.run(1, 0.05f);
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.25f).epsilon(0.02));

    // Stopping the runtime resets TimePosition for the next session.
    s.rt.stop();
    CHECK(snd->get_float("TimePosition") == doctest::Approx(0.0f));
}

TEST_CASE("audio: sounds added during play start, and missing files end at once") {
    Session s(false);
    const std::string wav = write_test_wav("late.wav", 8000, 0.5f);
    s.rt.start();

    Instance* late = s.dm.create("Sound", s.dm.workspace(), "Late");
    REQUIRE(late != nullptr);
    late->set("SoundId", Value::string(wav));
    late->set("Playing", Value::boolean(true));
    s.run(2, 0.1f);
    CHECK(late->get_float("TimePosition") == doctest::Approx(0.2f).epsilon(0.02));

    Instance* missing = s.dm.create("Sound", s.dm.workspace(), "Missing");
    REQUIRE(missing != nullptr);
    missing->set("SoundId", Value::string("does_not_exist.wav"));
    missing->set("Playing", Value::boolean(true));
    Instance* asset = s.dm.create("Sound", s.dm.workspace(), "Asset");
    REQUIRE(asset != nullptr);
    asset->set("SoundId", Value::string("rbxassetid://987"));
    asset->set("Playing", Value::boolean(true));
    s.run(1, 0.1f);
    CHECK_FALSE(missing->get_bool("Playing"));
    CHECK_FALSE(asset->get_bool("Playing"));
    CHECK(s.counts.ended == 2);
    CHECK(missing->get_float("TimeLength") == doctest::Approx(0.0f));
    s.rt.stop();
}

TEST_CASE("audio: PlayOnRemove plays a one-shot copy after the Sound is destroyed") {
    Session s(false);
    const std::string wav = write_test_wav("pop.wav", 8000, 0.5f);
    Instance* snd = s.dm.create("Sound", s.dm.workspace(), "Pop");
    REQUIRE(snd != nullptr);
    snd->set("SoundId", Value::string(wav));
    snd->set("PlayOnRemove", Value::boolean(true));
    s.rt.start();
    s.run(1, 0.1f);
    CHECK(s.audio->active_voices() == 1); // the tracked (silent) sound

    REQUIRE(s.dm.destroy(snd));
    s.run(1, 0.1f);
    CHECK(s.audio->active_voices() == 1); // the one-shot copy
    s.run(6, 0.1f);                       // 0.7 s: longer than the clip
    CHECK(s.audio->active_voices() == 0);
    s.rt.stop();
}

TEST_CASE("audio: the local character makes footsteps, and CharacterSounds turns them off") {
    Session s(true);
    Instance* ws = s.dm.workspace();
    Instance* floor = s.dm.create("Part", ws, "Floor");
    REQUIRE(floor != nullptr);
    floor->set("Position", Value::vector3(vec3{0.0f, -0.5f, 0.0f}));
    floor->set("Size", Value::vector3(vec3{200.0f, 1.0f, 200.0f}));
    floor->set("Anchored", Value::boolean(true));
    s.rt.start();

    const f32 dt = 1.0f / 60.0f;
    s.run(90, dt); // let the character land

    InputState walk{};
    walk.down[static_cast<usize>(Key::W)] = true;
    u32 peak = 0;
    for (int i = 0; i < 150; ++i) {
        s.rt.step(dt, walk);
        peak = std::max(peak, s.audio->active_voices());
    }
    CHECK(peak > 0);

    Instance* ss = s.dm.service("SoundService");
    REQUIRE(ss != nullptr);
    ss->set("CharacterSounds", Value::boolean(false));
    s.run(60, dt); // flush the sounds still playing
    peak = 0;
    for (int i = 0; i < 150; ++i) {
        s.rt.step(dt, walk);
        peak = std::max(peak, s.audio->active_voices());
    }
    CHECK(peak == 0);
    s.rt.stop();
}