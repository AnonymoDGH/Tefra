#include "tefra/studio/templates.h"
#include "tefra/studio/session.h"

#include "tefra/world/environment_sync.h"

#include <cmath>
#include <string>

namespace tefra::studio {

using world::DataModel;
using world::Instance;
using world::Value;

namespace {

// ---- Small builders -----------------------------------------------------------------------------------------------

void set_enum(Instance* i, const char* prop, const char* item) {
    const world::PropInfo* p = i->cls().find_prop(prop);
    if (!p || !p->enum_info) return;
    const i32 v = p->enum_info->index_of(item);
    if (v >= 0) i->set(prop, Value::enumeration(static_cast<u32>(v)));
}

Instance* part(DataModel& dm, Instance* parent, const char* name, vec3 pos, vec3 size, vec3 color,
               const char* material = "Plastic", const char* cls = "Part") {
    Instance* p = dm.create(cls, parent, name);
    p->set("Position", Value::vector3(pos));
    p->set("Size", Value::vector3(size));
    p->set("Color", Value::color3(color));
    p->set("Anchored", Value::boolean(true));
    set_enum(p, "Material", material);
    return p;
}

// A platform whose top face is at height `top`.
Instance* platform(DataModel& dm, Instance* parent, const char* name, vec3 center_top, vec3 size, vec3 color,
                   const char* material = "SmoothPlastic") {
    return part(dm, parent, name, {center_top.x, center_top.y - size.y * 0.5f, center_top.z}, size, color, material);
}

Instance* script(DataModel& dm, Instance* parent, const char* cls, const char* name, const char* source) {
    Instance* s = dm.create(cls, parent, name);
    s->set("Source", Value::string(source));
    return s;
}

Instance* sound(DataModel& dm, Instance* parent, const char* name, const char* id, f32 volume, bool looped = false,
                bool playing = false) {
    Instance* s = dm.create("Sound", parent, name);
    s->set("SoundId", Value::string(id));
    s->set("Volume", Value::number(volume));
    s->set("Looped", Value::boolean(looped));
    s->set("Playing", Value::boolean(playing));
    return s;
}

Instance* spawn(DataModel& dm, Instance* ws, vec3 top, vec3 color, const char* name = "SpawnLocation") {
    Instance* s =
        part(dm, ws, name, {top.x, top.y + 0.2f, top.z}, {6, 0.4f, 6}, color, "SmoothPlastic", "SpawnLocation");
    return s;
}

void lighting(DataModel& dm, f32 clock, f32 clouds, f32 fog = 0.0f) {
    Instance* l = dm.service("Lighting");
    l->set("ClockTime", Value::number(clock));
    l->set("CloudCover", Value::number(clouds));
    l->set("FogDensity", Value::number(fog));
}

// ---- Templates ----------------------------------------------------------------------------------------------------

void build_grey_baseplate(DataModel& dm) {
    Instance* ws = dm.workspace();
    platform(dm, ws, "Baseplate", {0, 0, 0}, {256, 1, 256}, {0.39f, 0.40f, 0.42f}, "Concrete");
    spawn(dm, ws, {0, 0, 0}, {0.15f, 0.45f, 0.85f});
    lighting(dm, 14.0f, 0.3f);
}

void build_classic(DataModel& dm) {
    Instance* ws = dm.workspace();
    platform(dm, ws, "Baseplate", {0, 0, 0}, {256, 1, 256}, {0.29f, 0.56f, 0.25f}, "Plastic");
    spawn(dm, ws, {0, 0, 0}, {0.64f, 0.64f, 0.66f});
    lighting(dm, 14.0f, 0.15f);
}

void build_flat_terrain(DataModel& dm) {
    Instance* ws = dm.workspace();
    platform(dm, ws, "Ground", {0, 0, 0}, {1024, 2, 1024}, {0.33f, 0.47f, 0.22f}, "Grass");
    spawn(dm, ws, {0, 0, 0}, {0.55f, 0.52f, 0.45f});
    // A few boulders so the ground has scale.
    Instance* rocks = dm.create("Folder", ws, "Rocks");
    u32 seed = 7u;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<f32>(seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < 14; ++i) {
        const f32 a = rnd() * 6.2831853f, r = 18.0f + rnd() * 70.0f, s = 1.2f + rnd() * 3.5f;
        Instance* rock = part(dm, rocks, "Rock", {std::cos(a) * r, s * 0.25f, std::sin(a) * r},
                              {s * (1.0f + rnd()), s, s * (1.0f + rnd())}, {0.42f, 0.41f, 0.39f}, "Rock");
        set_enum(rock, "Shape", "Ball");
        rock->set("Orientation", Value::vector3({0, rnd() * 360.0f, 0}));
    }
    lighting(dm, 15.0f, 0.35f, 0.0005f);
}

// Spawn on the landscape: the nearest spot to the origin whose height is in [lo, hi] and that is not steep, facing the
// lowest ground around it (the sea on the coast, the valley floor inland).
void spawn_on_terrain(DataModel& dm, const Instance& terrain, f32 lo, f32 hi) {
    auto h = [&](f32 x, f32 z) { return world::terrain_height(terrain, x, z); };
    vec3 best{0, h(0, 0), 0};
    bool found = false;
    for (f32 r = 0.0f; r <= 900.0f && !found; r += 15.0f) {
        const int steps = r == 0.0f ? 1 : static_cast<int>(r / 6.0f);
        for (int i = 0; i < steps && !found; ++i) {
            const f32 a = 6.2831853f * f32(i) / f32(steps);
            const f32 x = std::cos(a) * r, z = std::sin(a) * r, y = h(x, z);
            const f32 slope = std::fabs(h(x + 3, z) - y) + std::fabs(h(x, z + 3) - y);
            if (y >= lo && y <= hi && slope < 1.2f) {
                best = {x, y, z};
                found = true;
            }
        }
    }
    f32 low = 1e9f, yaw = 0.0f;
    for (int i = 0; i < 16; ++i) {
        const f32 a = 6.2831853f * f32(i) / 16.0f;
        const vec3 f{-std::sin(a), 0, -std::cos(a)}; // forward of yaw a
        const f32 y = h(best.x + f.x * 80.0f, best.z + f.z * 80.0f);
        if (y < low) {
            low = y;
            yaw = a;
        }
    }
    Instance* s = spawn(dm, dm.workspace(), best, {0.82f, 0.75f, 0.58f});
    set_enum(s, "Material", "Sand");
    if (world::terrain_height(terrain, best.x, best.z) > lo + 1e5f || lo < -1e5f) {
        // Inland (grass up to the knee): a raised stone platform the blades do not poke through.
        s->set("Size", Value::vector3({6, 1.4f, 6}));
        s->set("Position", Value::vector3({best.x, best.y + 0.6f, best.z}));
        s->set("Color", Value::color3({0.55f, 0.55f, 0.52f}));
        set_enum(s, "Material", "Slate");
    }
    s->set("Orientation", Value::vector3({0, yaw * 57.29578f, 0}));
}

void build_beach(DataModel& dm) {
    Instance* ws = dm.workspace();
    Instance* t = dm.create("Terrain", ws, "Terrain");
    set_enum(t, "Kind", "Coast");
    dm.create("Ocean", ws, "Ocean");
    lighting(dm, 16.0f, 0.38f);
    spawn_on_terrain(dm, *t, t->get_float("SeaLevel", 0.0f) + 1.2f, t->get_float("SeaLevel", 0.0f) + 3.5f);
}

void build_meadow(DataModel& dm) {
    Instance* ws = dm.workspace();
    Instance* t = dm.create("Terrain", ws, "Terrain");
    set_enum(t, "Kind", "Plains");
    lighting(dm, 17.2f, 0.35f);
    spawn_on_terrain(dm, *t, -1e6f, 1e6f);
}

void build_valley(DataModel& dm) {
    Instance* ws = dm.workspace();
    Instance* t = dm.create("Terrain", ws, "Terrain");
    set_enum(t, "Kind", "Valley");
    lighting(dm, 9.0f, 0.3f, 0.008f);
    spawn_on_terrain(dm, *t, -1e6f, 1e6f);
}

// ---- Obby: a complete example game ----------------------------------------------------------------------------
// Four stages along -Z (jumps, kill bricks, moving platforms + a spinning bar, a climb and narrow beams), three
// checkpoints, lava below, a gold finish, a HUD with stage, timer and reset button, and sounds. Every behaviour is
// a C# script in the place, so it doubles as a scripting tutorial.

const char* kConfigModule = R"cs(using Tefra;

// ModuleScript: shared settings, visible to every script in the game.
public static class ObbyConfig
{
    public const int Stages = 4;
    public const float MoverAmplitude = 5f;   // metres each side
    public const float MoverSpeed = 1.1f;     // radians per second
    public const float SpinnerSpeed = 110f;   // degrees per second
}
)cs";

const char* kKillBrick = R"cs(using Tefra;

// Touching this part kills the character (it respawns at the last checkpoint).
public class KillBrick : TefraScript
{
    public override void Start()
    {
        var part = (BasePart)Parent;
        part.Touched += other =>
        {
            var humanoid = other.Parent?.FindFirstChildOfClass("Humanoid") as Humanoid;
            if (humanoid != null) humanoid.Health = 0;
        };
    }
}
)cs";

const char* kCheckpoint = R"cs(using Tefra;

// Touching a checkpoint makes it the player's respawn point.
public class Checkpoint : TefraScript
{
    public override void Start()
    {
        var pad = (SpawnLocation)Parent;
        pad.Touched += other =>
        {
            var player = Players.GetPlayerFromCharacter(other.Parent);
            if (player == null || player.RespawnLocation == pad) return;
            player.RespawnLocation = pad;
            pad.Color = new Color3(0.25f, 0.85f, 0.35f);
            var chime = pad.FindFirstChildOfClass("Sound") as Sound;
            chime?.Play();
        };
    }
}
)cs";

const char* kMover = R"cs(using System;
using Tefra;

// Slides the platform left and right. Platforms named "...B" move in opposite phase.
public class Mover : TefraScript
{
    Vector3 start;
    float t;

    public override void Start()
    {
        start = ((BasePart)Parent).Position;
        if (Parent.Name.EndsWith("B")) t = MathF.PI;
    }

    public override void Update(float dt)
    {
        t += dt * ObbyConfig.MoverSpeed;
        ((BasePart)Parent).Position = start + new Vector3(MathF.Sin(t) * ObbyConfig.MoverAmplitude, 0, 0);
    }
}
)cs";

const char* kSpinner = R"cs(using Tefra;

// Spins the bar around the vertical axis.
public class Spinner : TefraScript
{
    public override void Update(float dt)
    {
        var bar = (BasePart)Parent;
        bar.Orientation += new Vector3(0, ObbyConfig.SpinnerSpeed * dt, 0);
    }
}
)cs";

const char* kFinish = R"cs(using Tefra;

// Reaching the gold pad shows the win screen once.
public class Finish : TefraScript
{
    bool done;

    public override void Start()
    {
        var pad = (BasePart)Parent;
        pad.Touched += other =>
        {
            if (done || Players.GetPlayerFromCharacter(other.Parent) == null) return;
            done = true;
            var hud = Players.LocalPlayer.FindFirstChildOfClass("PlayerGui")?.FindFirstChild("Hud");
            var win = hud?.FindFirstChild("Win") as Frame;
            if (win != null) win.Visible = true;
            (pad.FindFirstChildOfClass("Sound") as Sound)?.Play();
            (pad.FindFirstChild("Confetti") as ParticleEmitter)?.Emit(250);
            Debug.Log("Obby completed!");
        };
    }
}
)cs";

const char* kHud = R"cs(using Tefra;

// LocalScript: lives in the player's screen (StarterGui is copied into PlayerGui when the game starts).
public class Hud : TefraScript
{
    TextLabel stage, timer, winTime, hint;
    Frame win;
    float t;

    public override void Start()
    {
        var gui = Script.Parent;
        var bar = gui.FindFirstChild("TopBar");
        stage = bar.FindFirstChild("Stage") as TextLabel;
        timer = bar.FindFirstChild("Timer") as TextLabel;
        win = gui.FindFirstChild("Win") as Frame;
        winTime = win.FindFirstChild("Time") as TextLabel;
        hint = gui.FindFirstChild("Hint") as TextLabel;
        var reset = gui.FindFirstChild("Reset") as TextButton;
        reset.Activated += () =>
        {
            (gui.FindFirstChild("Click") as Sound)?.Play();
            var humanoid = Players.LocalPlayer.Character?.FindFirstChildOfClass("Humanoid") as Humanoid;
            if (humanoid != null) humanoid.Health = 0;
        };
    }

    public override void Update(float dt)
    {
        if (!win.Visible) t += dt;
        timer.Text = $"{t:0.0} s";
        winTime.Text = $"Time: {t:0.0} s";
        var checkpoint = Players.LocalPlayer.RespawnLocation;
        int n = 1;
        if (checkpoint != null && checkpoint.Name.StartsWith("Checkpoint"))
            n = int.Parse(checkpoint.Name.Substring("Checkpoint".Length)) + 1;
        stage.Text = win.Visible ? "Finished!" : $"Stage {n} / {ObbyConfig.Stages}";
        if (t > 8f) hint.Visible = false;
    }
}
)cs";

Instance* gui_label(DataModel& dm, Instance* parent, const char* cls, const char* name, const char* text, vec3 pos,
                    vec3 pos_off, vec3 size, vec3 size_off, f32 text_size, vec3 color) {
    Instance* l = dm.create(cls, parent, name);
    l->set("Position", Value::vector3(pos));
    l->set("PositionOffset", Value::vector3(pos_off));
    l->set("Size", Value::vector3(size));
    l->set("SizeOffset", Value::vector3(size_off));
    if (l->is_a("TextLabel")) {
        l->set("Text", Value::string(text));
        l->set("TextSize", Value::number(text_size));
        l->set("TextColor", Value::color3(color));
    }
    return l;
}

void build_obby(DataModel& dm) {
    Instance* ws = dm.workspace();
    ws->set("FallenPartsDestroyHeight", Value::number(-25.0));
    Instance* sp = dm.service("StarterPlayer");
    sp->set("CharacterWalkSpeed", Value::number(8.0));
    sp->set("CharacterJumpHeight", Value::number(2.0));
    dm.service("Players")->set("RespawnTime", Value::number(1.5));
    lighting(dm, 16.5f, 0.45f);
    script(dm, dm.service("ReplicatedStorage"), "ModuleScript", "ObbyConfig", kConfigModule);

    const vec3 white{0.92f, 0.92f, 0.94f}, red{1.0f, 0.18f, 0.12f}, gold{1.0f, 0.78f, 0.2f};
    const vec3 rainbow[] = {{0.95f, 0.3f, 0.3f},   {1.0f, 0.6f, 0.15f}, {1.0f, 0.85f, 0.2f},
                            {0.35f, 0.85f, 0.35f}, {0.25f, 0.6f, 1.0f}, {0.55f, 0.4f, 0.95f}};
    auto kill = [&](Instance* p) {
        p->set("Color", Value::color3(red));
        set_enum(p, "Material", "Neon");
        script(dm, p, "Script", "KillBrick", kKillBrick);
        return p;
    };
    auto checkpoint = [&](int n, vec3 top) {
        platform(dm, ws, "CheckpointPlatform", top, {10, 1, 10}, white);
        Instance* cp = part(dm, ws, ("Checkpoint" + std::to_string(n)).c_str(), {top.x, top.y + 0.2f, top.z},
                            {5, 0.4f, 5}, {0.35f, 0.38f, 0.42f}, "SmoothPlastic", "SpawnLocation");
        cp->set("Enabled", Value::boolean(false)); // only a respawn point once touched
        script(dm, cp, "Script", "Checkpoint", kCheckpoint);
        sound(dm, cp, "Chime", "builtin:checkpoint", 0.6f);
    };

    // Start.
    platform(dm, ws, "Start", {0, 0, 0}, {16, 1, 16}, white);
    spawn(dm, ws, {0, 0, 0}, {0.15f, 0.45f, 0.85f});
    // Lava far below (falling also kills at FallenPartsDestroyHeight).
    Instance* lava = part(dm, ws, "Lava", {0, -16, -110}, {400, 1, 500}, {1.0f, 0.35f, 0.05f}, "Neon");
    script(dm, lava, "Script", "KillBrick", kKillBrick);

    // Stage 1: rising jumps.
    Instance* s1 = dm.create("Model", ws, "Stage1");
    const f32 z1[] = {-14, -21, -28, -35};
    for (int i = 0; i < 4; ++i)
        platform(dm, s1, "Jump", {i % 2 ? 1.5f : -1.5f, 0.6f * f32(i + 1), z1[i]}, {4, 1, 4}, rainbow[i]);
    checkpoint(1, {0, 2.4f, -46});

    // Stage 2: kill-brick corridor.
    Instance* s2 = dm.create("Model", ws, "Stage2");
    platform(dm, s2, "Corridor", {0, 2.4f, -66}, {6, 1, 30}, white);
    for (int i = 0; i < 2; ++i)
        platform(dm, s2, "Rail", {i ? 3.25f : -3.25f, 2.9f, -66}, {0.5f, 0.5f, 30}, {0.3f, 0.32f, 0.36f});
    const f32 zk[] = {-56, -61, -66, -71, -76};
    for (int i = 0; i < 5; ++i) {
        const f32 w = i == 2 ? 4.0f : 6.0f; // the middle one leaves a gap on the side
        kill(part(dm, s2, "KillBrick", {i == 2 ? -1.0f : 0.0f, 2.7f, zk[i]}, {w, 0.6f, 1.0f}, red));
    }
    checkpoint(2, {0, 2.4f, -90});

    // Stage 3: moving platforms, then a spinning bar.
    Instance* s3 = dm.create("Model", ws, "Stage3");
    Instance* ma = platform(dm, s3, "MoverA", {0, 2.4f, -102}, {6, 1, 6}, rainbow[4]);
    script(dm, ma, "Script", "Mover", kMover);
    Instance* mb = platform(dm, s3, "MoverB", {0, 2.4f, -113}, {6, 1, 6}, rainbow[5]);
    script(dm, mb, "Script", "Mover", kMover);
    Instance* disc = platform(dm, s3, "SpinnerDisc", {0, 2.4f, -128}, {1, 1, 1}, white);
    set_enum(disc, "Shape", "Cylinder");
    disc->set("Size", Value::vector3({16, 1, 16}));
    Instance* bar = kill(part(dm, s3, "SpinnerBar", {0, 3.0f, -128}, {15, 0.5f, 0.6f}, red));
    bar->set("CanCollide", Value::boolean(false));
    script(dm, bar, "Script", "Spinner", kSpinner);
    checkpoint(3, {0, 2.4f, -142});

    // Stage 4: climb, then narrow beams.
    Instance* s4 = dm.create("Model", ws, "Stage4");
    for (int i = 0; i < 5; ++i)
        platform(dm, s4, "Step", {i % 2 ? 2.5f : -2.5f, 3.6f + 1.2f * f32(i), -151.0f - 4.0f * f32(i)}, {3, 1, 3},
                 rainbow[i]);
    platform(dm, s4, "Landing", {0, 8.4f, -176}, {8, 1, 6}, white);
    platform(dm, s4, "Beam", {0, 8.4f, -186}, {0.9f, 1, 14}, rainbow[2]);
    platform(dm, s4, "Beam", {2.0f, 8.4f, -203}, {0.9f, 1, 14}, rainbow[3]);

    // Finish.
    Instance* fin = dm.create("Model", ws, "Finish");
    platform(dm, fin, "FinishPlatform", {0, 8.4f, -218}, {12, 1, 12}, gold);
    Instance* pad = part(dm, fin, "FinishPad", {0, 8.6f, -218}, {6, 0.4f, 6}, gold, "Neon");
    script(dm, pad, "Script", "Finish", kFinish);
    {
        Instance* glow = dm.create("ParticleEmitter", pad, "Sparkles");
        glow->set("Texture", Value::string("builtin:star"));
        glow->set("Rate", Value::number(14.0));
        glow->set("Lifetime", Value::number(2.2));
        glow->set("Speed", Value::number(1.6));
        glow->set("SpreadAngle", Value::number(25.0));
        glow->set("Size", Value::number(0.45));
        glow->set("SizeEnd", Value::number(0.0));
        glow->set("Color", Value::color3({1.0f, 0.86f, 0.35f}));
        glow->set("ColorEnd", Value::color3({1.0f, 0.62f, 0.2f}));
        glow->set("TransparencyEnd", Value::number(0.3));
        glow->set("LightEmission", Value::number(1.0));
        glow->set("RotSpeed", Value::number(60.0));
        Instance* confetti = dm.create("ParticleEmitter", pad, "Confetti");
        confetti->set("Enabled", Value::boolean(false));
        confetti->set("Texture", Value::string("builtin:snow"));
        confetti->set("Lifetime", Value::number(3.0));
        confetti->set("Speed", Value::number(14.0));
        confetti->set("SpreadAngle", Value::number(40.0));
        confetti->set("Size", Value::number(0.25));
        confetti->set("SizeEnd", Value::number(0.2));
        confetti->set("Color", Value::color3({0.3f, 0.8f, 1.0f}));
        confetti->set("ColorEnd", Value::color3({1.0f, 0.35f, 0.6f}));
        confetti->set("TransparencyEnd", Value::number(0.4));
        confetti->set("Acceleration", Value::vector3({0, -9.0f, 0}));
        confetti->set("Drag", Value::number(1.2));
        confetti->set("Variation", Value::number(0.5));
    }
    sound(dm, pad, "Fanfare", "builtin:win", 0.8f);
    for (int i = 0; i < 2; ++i) part(dm, fin, "Pillar", {i ? 5.0f : -5.0f, 12.4f, -223}, {1, 8, 1}, white, "Marble");
    part(dm, fin, "Arch", {0, 16.9f, -223}, {11, 1, 1}, gold, "Neon");

    // Ambience.
    sound(dm, ws, "Ambience", "builtin:wind_loop", 0.25f, true, true);

    // HUD.
    Instance* hud = dm.create("ScreenGui", dm.service("StarterGui"), "Hud");
    hud->set("ResetOnSpawn", Value::boolean(false));
    Instance* top = gui_label(dm, hud, "Frame", "TopBar", "", {0.5f, 0, 0}, {0, 12, 0}, {0, 0, 0}, {380, 56, 0}, 0, {});
    top->set("AnchorPoint", Value::vector3({0.5f, 0, 0}));
    top->set("BackgroundColor", Value::color3({0.08f, 0.09f, 0.11f}));
    top->set("BackgroundTransparency", Value::number(0.3));
    top->set("CornerRadius", Value::number(14.0));
    Instance* st = gui_label(dm, top, "TextLabel", "Stage", "Stage 1 / 4", {0, 0, 0}, {18, 0, 0}, {0.6f, 1, 0},
                             {0, 0, 0}, 26, {1, 1, 1});
    st->set("BackgroundTransparency", Value::number(1.0));
    set_enum(st, "TextXAlignment", "Left");
    set_enum(st, "Font", "Bold");
    Instance* tm = gui_label(dm, top, "TextLabel", "Timer", "0.0 s", {0.6f, 0, 0}, {-18, 0, 0}, {0.4f, 1, 0}, {0, 0, 0},
                             24, {1.0f, 0.85f, 0.35f});
    tm->set("BackgroundTransparency", Value::number(1.0));
    set_enum(tm, "TextXAlignment", "Right");
    set_enum(tm, "Font", "Mono");
    Instance* rb = gui_label(dm, hud, "TextButton", "Reset", "Reset", {1, 0, 0}, {-14, 14, 0}, {0, 0, 0}, {120, 46, 0},
                             22, {1, 1, 1});
    rb->set("AnchorPoint", Value::vector3({1, 0, 0}));
    rb->set("BackgroundColor", Value::color3({0.85f, 0.25f, 0.22f}));
    rb->set("CornerRadius", Value::number(12.0));
    set_enum(rb, "Font", "Bold");
    Instance* hint = gui_label(dm, hud, "TextLabel", "Hint", "WASD move  -  Space jump  -  reach the gold platform!",
                               {0.5f, 1, 0}, {0, -24, 0}, {0, 0, 0}, {620, 40, 0}, 20, {1, 1, 1});
    hint->set("AnchorPoint", Value::vector3({0.5f, 1, 0}));
    hint->set("BackgroundColor", Value::color3({0.08f, 0.09f, 0.11f}));
    hint->set("BackgroundTransparency", Value::number(0.45));
    hint->set("CornerRadius", Value::number(10.0));
    Instance* win = gui_label(dm, hud, "Frame", "Win", "", {0.5f, 0.45f, 0}, {}, {0, 0, 0}, {440, 210, 0}, 0, {});
    win->set("AnchorPoint", Value::vector3({0.5f, 0.5f, 0}));
    win->set("BackgroundColor", Value::color3({0.08f, 0.09f, 0.11f}));
    win->set("BackgroundTransparency", Value::number(0.15));
    win->set("CornerRadius", Value::number(18.0));
    win->set("BorderColor", Value::color3(gold));
    win->set("BorderSize", Value::number(3.0));
    win->set("Visible", Value::boolean(false));
    Instance* title =
        gui_label(dm, win, "TextLabel", "Title", "You win!", {0, 0.12f, 0}, {}, {1, 0.45f, 0}, {0, 0, 0}, 52, gold);
    title->set("BackgroundTransparency", Value::number(1.0));
    set_enum(title, "Font", "Bold");
    title->set("TextStrokeTransparency", Value::number(0.4));
    Instance* wt = gui_label(dm, win, "TextLabel", "Time", "Time: 0.0 s", {0, 0.58f, 0}, {}, {1, 0.3f, 0}, {0, 0, 0},
                             28, {1, 1, 1});
    wt->set("BackgroundTransparency", Value::number(1.0));
    sound(dm, hud, "Click", "builtin:click", 0.7f);
    script(dm, hud, "LocalScript", "Hud", kHud);
}

const std::vector<TemplateInfo> kTemplates = {
    {"baseplate", "Baseplate", "A grey baseplate and a spawn: the usual starting point.", "Basic"},
    {"classic_baseplate", "Classic Baseplate", "The bright green baseplate of old-school games.", "Basic"},
    {"flat_terrain", "Flat Terrain", "A huge grass field with a few boulders.", "Basic"},
    {"obby", "Obby", "A complete obstacle course: checkpoints, kill bricks, movers, HUD and sounds.", "Game"},
    {"beach", "Beach", "Tropical coast with palms, surf and an animated ocean.", "Environment"},
    {"meadow", "Meadow", "Rolling grassland covered in tall grass and wildflowers that sway in the wind.",
     "Environment"},
    {"valley", "Valley", "Misty forest valley with trees, grass and morning light.", "Environment"},
};

} // namespace

const std::vector<TemplateInfo>& templates() { return kTemplates; }

const TemplateInfo* find_template(std::string_view id) {
    for (const TemplateInfo& t : kTemplates)
        if (id == t.id) return &t;
    return nullptr;
}

bool build_template(std::string_view id, DataModel& dm) {
    if (id == "baseplate")
        build_grey_baseplate(dm);
    else if (id == "classic_baseplate")
        build_classic(dm);
    else if (id == "flat_terrain")
        build_flat_terrain(dm);
    else if (id == "obby")
        build_obby(dm);
    else if (id == "beach")
        build_beach(dm);
    else if (id == "valley")
        build_valley(dm);
    else if (id == "meadow")
        build_meadow(dm);
    else
        return false;
    return true;
}

void build_baseplate(DataModel& model) { build_template("baseplate", model); }

} // namespace tefra::studio
