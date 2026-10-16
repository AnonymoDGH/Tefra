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

// ---- Backrooms: endless yellow office rooms under buzzing fluorescent panels -------------------------------------

const char* kFlicker = R"cs(using Tefra;

// A dying fluorescent tube: mostly on, with irregular stutters and short blackouts.
public class Flicker : TefraScript
{
    System.Random rng = new System.Random();
    BasePart panel;
    Instance lamp;
    Color3 on;
    float t, next = 1f;

    public override void Start()
    {
        panel = (BasePart)Parent;
        lamp = panel.FindFirstChild("Source")?.FindFirstChildOfClass("PointLight");
        on = panel.Color;
    }

    public override void Update(float dt)
    {
        t += dt;
        if (t < next) return;
        t = 0f;
        bool off = rng.NextDouble() < 0.4;
        if (lamp != null) lamp["Enabled"] = !off;
        panel.Color = off ? new Color3(0.22f, 0.21f, 0.18f) : on;
        next = off ? (float)(0.03 + rng.NextDouble() * 0.15) : (float)(0.04 + rng.NextDouble() * (rng.NextDouble() < 0.7 ? 0.25 : 3.0));
    }
}
)cs";

void build_backrooms(DataModel& dm) {
    Instance* ws = dm.workspace();
    constexpr int kCells = 18;     // per side
    constexpr f32 kCell = 5.0f;    // m
    constexpr f32 kHeight = 2.75f; // floor to ceiling
    constexpr f32 kWall = 0.18f;
    const f32 half = f32(kCells) * kCell * 0.5f;
    const f32 span = f32(kCells) * kCell;
    const vec3 wall_col{0.80f, 0.72f, 0.40f}, carpet_col{0.50f, 0.45f, 0.28f}, ceiling_col{0.86f, 0.83f, 0.70f};
    const vec3 base_col{0.42f, 0.33f, 0.18f};
    u32 seed = 0x5EEDu;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return f32(seed >> 8) / 16777216.0f;
    };

    Instance* rooms = dm.create("Model", ws, "Level0");
    part(dm, rooms, "Carpet", {0, -0.05f, 0}, {span + 2, 0.1f, span + 2}, carpet_col, "Carpet");
    part(dm, rooms, "Ceiling", {0, kHeight + 0.05f, 0}, {span + 2, 0.1f, span + 2}, ceiling_col, "CeilingTile");
    // Outer walls.
    for (int side = 0; side < 4; ++side) {
        const bool x_axis = side < 2;
        const f32 off = (side % 2 == 0 ? -1.0f : 1.0f) * (half + kWall * 0.5f);
        const vec3 pos = x_axis ? vec3{0, kHeight * 0.5f, off} : vec3{off, kHeight * 0.5f, 0};
        const vec3 size = x_axis ? vec3{span + 1, kHeight, kWall} : vec3{kWall, kHeight, span + 1};
        part(dm, rooms, "OuterWall", pos, size, wall_col, "Wallpaper");
    }
    // Props modelled in Blender (tools/blender/make_backrooms_props.py), shipped in resources/models/backrooms.
    auto prop = [&dm](Instance* parent, const char* name, const char* model, vec3 pos, f32 yaw) {
        Instance* m = dm.create("MeshPart", parent, name);
        m->set("MeshId", Value::string(std::string("builtin:models/backrooms/") + model + ".glb"));
        m->set("Size", Value::vector3({1, 1, 1}));
        m->set("Position", Value::vector3(pos));
        m->set("Orientation", Value::vector3({0, yaw, 0}));
        m->set("Anchored", Value::boolean(true));
        return m;
    };
    // Interior walls on the cell edges: solid runs, doorways (frame + header above), some missing, a few pillars.
    // Outlets near the floor and switches beside the doorways, on random faces.
    Instance* walls = dm.create("Folder", rooms, "Walls");
    Instance* fixtures = dm.create("Folder", rooms, "Fixtures");
    constexpr f32 kDoor = 0.92f, kDoorH = 2.1f;
    auto wall_run = [&](bool along_x, f32 x, f32 z, f32 a0, f32 a1) { // a0..a1 along the edge, from (x, z)
        const f32 len = a1 - a0;
        if (len <= 0.05f) return;
        const f32 mid = (a0 + a1) * 0.5f;
        const vec3 c = along_x ? vec3{x + mid, kHeight * 0.5f, z} : vec3{x, kHeight * 0.5f, z + mid};
        const vec3 size = along_x ? vec3{len, kHeight, kWall} : vec3{kWall, kHeight, len};
        part(dm, walls, "Wall", c, size, wall_col, "Wallpaper");
        const vec3 bsize = along_x ? vec3{len, 0.12f, kWall + 0.03f} : vec3{kWall + 0.03f, 0.12f, len};
        part(dm, walls, "Baseboard", {c.x, 0.06f, c.z}, bsize, base_col, "Wood");
        if (len > 1.5f && rnd() < 0.35f) {
            const f32 side = rnd() < 0.5f ? -1.0f : 1.0f;
            const f32 at = a0 + 0.4f + rnd() * (len - 0.8f);
            const f32 off = side * (kWall * 0.5f + 0.001f);
            // Plate fronts face the prop's -Z: yaw turns them towards the room on that side.
            const vec3 p = along_x ? vec3{x + at, 0.32f, z + off} : vec3{x + off, 0.32f, z + at};
            const f32 yaw = along_x ? (side > 0 ? 180.0f : 0.0f) : (side > 0 ? -90.0f : 90.0f);
            prop(fixtures, "Outlet", "outlet", p, yaw);
        }
    };
    auto edge = [&](bool along_x, f32 x, f32 z) {
        const f32 r = rnd();
        if (r < 0.5f) return; // open
        if (r < 0.78f) {      // solid
            wall_run(along_x, x, z, -kWall * 0.5f, kCell + kWall * 0.5f);
            return;
        }
        // Doorway somewhere along the edge.
        const f32 d0 = 0.5f + rnd() * (kCell - kDoor - 1.0f), d1 = d0 + kDoor;
        wall_run(along_x, x, z, -kWall * 0.5f, d0);
        wall_run(along_x, x, z, d1, kCell + kWall * 0.5f);
        const f32 dm_ = (d0 + d1) * 0.5f;
        const vec3 head =
            along_x ? vec3{x + dm_, (kHeight + kDoorH) * 0.5f, z} : vec3{x, (kHeight + kDoorH) * 0.5f, z + dm_};
        const vec3 hsize = along_x ? vec3{kDoor, kHeight - kDoorH, kWall} : vec3{kWall, kHeight - kDoorH, kDoor};
        part(dm, walls, "Header", head, hsize, wall_col, "Wallpaper");
        prop(fixtures, "DoorFrame", "door_frame", along_x ? vec3{x + dm_, 0, z} : vec3{x, 0, z + dm_},
             along_x ? 0.0f : 90.0f);
        if (rnd() < 0.5f) {
            const f32 side = rnd() < 0.5f ? -1.0f : 1.0f, off = side * (kWall * 0.5f + 0.001f);
            const f32 at = d1 + 0.18f;
            if (at < kCell - 0.1f) {
                const vec3 p = along_x ? vec3{x + at, 1.2f, z + off} : vec3{x + off, 1.2f, z + at};
                const f32 yaw = along_x ? (side > 0 ? 180.0f : 0.0f) : (side > 0 ? -90.0f : 90.0f);
                prop(fixtures, "Switch", "switch", p, yaw);
            }
        }
    };
    for (int gx = 0; gx <= kCells; ++gx)
        for (int gz = 0; gz <= kCells; ++gz) {
            const f32 x = -half + f32(gx) * kCell, z = -half + f32(gz) * kCell;
            const bool border_x = gx == 0 || gx == kCells, border_z = gz == 0 || gz == kCells;
            if (gx < kCells && !border_z) edge(true, x, z);
            if (gz < kCells && !border_x) edge(false, x, z);
            if (!border_x && !border_z && rnd() < 0.06f)
                part(dm, walls, "Pillar", {x, kHeight * 0.5f, z}, {0.5f, kHeight, 0.5f}, wall_col, "Wallpaper");
        }
    // Fluorescent troffers: one per cell, a few dead, some flickering (a Neon lens over the fixture lets the script
    // switch it); return-air vents and the odd abandoned chair.
    Instance* lights = dm.create("Folder", rooms, "Lights");
    for (int cx = 0; cx < kCells; ++cx)
        for (int cz = 0; cz < kCells; ++cz) {
            const f32 x = -half + (f32(cx) + 0.5f) * kCell, z = -half + (f32(cz) + 0.5f) * kCell;
            const f32 r = rnd();
            const bool dead = r < 0.06f, flicker = !dead && r < 0.12f;
            const f32 yaw = rnd() < 0.5f ? 0.0f : 90.0f;
            Instance* fixture =
                prop(lights, "Troffer", "troffer", {x, kHeight - 0.013f, z}, yaw); // lens just below the tiles
            fixture->set("CastShadow", Value::boolean(false));
            Instance* holder = fixture;
            if (dead || flicker) {
                Instance* lens =
                    part(dm, fixture, "Lens", {x, kHeight - 0.016f, z},
                         yaw == 0.0f ? vec3{0.54f, 0.004f, 1.16f} : vec3{1.16f, 0.004f, 0.54f},
                         dead ? vec3{0.32f, 0.31f, 0.27f} : vec3{1.0f, 0.95f, 0.8f}, dead ? "SmoothPlastic" : "Neon");
                lens->set("CastShadow", Value::boolean(false));
                holder = lens;
            }
            if (!dead) {
                // The light hangs 25 cm under the fixture (an invisible part) so the tiles around it catch light too.
                Instance* src = part(dm, holder, "Source", {x, kHeight - 0.25f, z}, {0.1f, 0.1f, 0.1f}, {1, 1, 1});
                src->set("Transparency", Value::number(1.0));
                src->set("CanCollide", Value::boolean(false));
                src->set("CastShadow", Value::boolean(false));
                Instance* lamp = dm.create("PointLight", src, "Light");
                lamp->set("Color", Value::color3({1.0f, 0.93f, 0.76f}));
                lamp->set("Brightness", Value::number(3.4 + rnd() * 0.8));
                lamp->set("Range", Value::number(11.0));
                if (flicker) script(dm, holder, "Script", "Flicker", kFlicker);
            }
            if (rnd() < 0.18f) {
                const f32 vx = x + (rnd() < 0.5f ? -1.6f : 1.6f), vz = z + (rnd() - 0.5f) * 2.0f;
                prop(fixtures, "Vent", "vent", {vx, kHeight - 0.006f, vz}, rnd() < 0.5f ? 0.0f : 90.0f);
            }
            if (rnd() < 0.03f)
                prop(fixtures, "Chair", "chair", {x + (rnd() - 0.5f) * 3.0f, 0, z + (rnd() - 0.5f) * 3.0f},
                     rnd() * 360.0f);
        }
    // Spawn on the carpet, nearly invisible.
    Instance* s = part(dm, ws, "SpawnLocation", {kCell * 0.5f, 0.02f, kCell * 0.5f}, {3, 0.04f, 3}, carpet_col,
                       "Carpet", "SpawnLocation");
    (void)s;
    sound(dm, ws, "Hum", "builtin:hum_loop", 0.35f, true, true);
    // Lighting: no daylight, warm bounce light, a faint haze that swallows the distance.
    Instance* l = dm.service("Lighting");
    l->set("ClockTime", Value::number(14.0));
    l->set("CloudCover", Value::number(0.0));
    l->set("SunBrightness", Value::number(0.0));
    l->set("EnvironmentDiffuseScale", Value::number(0.0));
    l->set("IndoorAmbient", Value::color3({1.0f, 0.88f, 0.58f}));
    l->set("IndoorAmbientBrightness", Value::number(0.22));
    l->set("ExposureCompensation", Value::number(7.5));
    l->set("FogDensity", Value::number(0.035));
    l->set("FogHeightFalloff", Value::number(0.0));
    l->set("Saturation", Value::number(0.95));
    l->set("Contrast", Value::number(1.0));
}

// ---- Rainy city at night: wet streets, street lamps, neon, lit windows, parked cars ------------------------------

const char* kSteam = R"cs(using Tefra;

// Steam from a manhole: puffs come and go.
public class Steam : TefraScript
{
    System.Random rng = new System.Random();
    float t;

    public override void Update(float dt)
    {
        t += dt;
        if (t < 0.5f) return;
        t = 0f;
        var e = Parent.FindFirstChildOfClass("ParticleEmitter") as ParticleEmitter;
        if (e != null) e.Rate = (float)(4 + rng.NextDouble() * 8);
    }
}
)cs";

struct CityBuilding {
    const char* model;
    f32 height; // metres, roof parapet included
};
// Must match CATALOGUE in tools/blender/make_city_buildings.py (3 m floors + 0.75 m crown).
constexpr CityBuilding kCityBuildings[] = {{"apt_a", 18.75f},     {"apt_b", 24.75f},  {"apt_c", 15.75f},
                                           {"apt_tower", 42.75f}, {"loft_a", 15.3f},  {"loft_b", 21.3f},
                                           {"loft_tower", 30.3f}, {"warehouse", 9.3f}};
constexpr u32 kCityBuildingCount = sizeof(kCityBuildings) / sizeof(kCityBuildings[0]);
constexpr f32 kBuilding = 18.0f;

void build_city_rain(DataModel& dm) {
    Instance* ws = dm.workspace();
    constexpr int kBlocks = 5;
    constexpr f32 kLot = 46.0f, kStreet = 16.0f, kPitch = kLot + kStreet, kSidewalk = 3.0f, kCurb = 0.15f;
    const f32 span = f32(kBlocks) * kPitch;
    u32 seed = 0xC17Bu;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return f32(seed >> 8) / 16777216.0f;
    };
    auto yaw_to = [](vec3 d) { return degrees(std::atan2(-d.x, -d.z)); };
    auto prop = [&dm](Instance* parent, const char* name, const char* model, vec3 pos, f32 yaw) {
        Instance* m = dm.create("MeshPart", parent, name);
        m->set("MeshId", Value::string(std::string("builtin:models/city/") + model + ".glb"));
        m->set("Size", Value::vector3({1, 1, 1}));
        m->set("Position", Value::vector3(pos));
        m->set("Orientation", Value::vector3({0, yaw, 0}));
        m->set("Anchored", Value::boolean(true));
        return m;
    };
    auto light_at = [&dm](Instance* parent, vec3 pos, vec3 color, f32 brightness, f32 range) {
        Instance* src = dm.create("Part", parent, "LightSource");
        src->set("Position", Value::vector3(pos));
        src->set("Size", Value::vector3({0.1f, 0.1f, 0.1f}));
        src->set("Transparency", Value::number(1.0));
        src->set("CanCollide", Value::boolean(false));
        src->set("CastShadow", Value::boolean(false));
        src->set("Anchored", Value::boolean(true));
        Instance* l = dm.create("PointLight", src, "Light");
        l->set("Color", Value::color3(color));
        l->set("Brightness", Value::number(brightness));
        l->set("Range", Value::number(range));
        return src;
    };
    // Shaped light: a SpotLight whose source part looks along `orient` (pitch, yaw), with spill outside the cone and
    // optionally a shadow map (street lamps: the cap blocks the light upwards, the post and cars cast shadows).
    auto spot_at = [&dm](Instance* parent, vec3 pos, vec3 orient, vec3 color, f32 brightness, f32 range, f32 angle,
                         f32 spill, bool shadows) {
        Instance* src = dm.create("Part", parent, "LightSource");
        src->set("Position", Value::vector3(pos));
        src->set("Orientation", Value::vector3(orient));
        src->set("Size", Value::vector3({0.1f, 0.1f, 0.1f}));
        src->set("Transparency", Value::number(1.0));
        src->set("CanCollide", Value::boolean(false));
        src->set("CastShadow", Value::boolean(false));
        src->set("Anchored", Value::boolean(true));
        Instance* l = dm.create("SpotLight", src, "Light");
        l->set("Color", Value::color3(color));
        l->set("Brightness", Value::number(brightness));
        l->set("Range", Value::number(range));
        l->set("Angle", Value::number(angle));
        l->set("Spill", Value::number(spill));
        l->set("Shadows", Value::boolean(shadows));
        return src;
    };
    auto center = [&](int i) { return (f32(i) - f32(kBlocks - 1) * 0.5f) * kPitch; };

    Instance* city = dm.create("Model", ws, "City");
    Instance* streets = dm.create("Folder", city, "Streets");
    part(dm, streets, "Road", {0, -0.1f, 0}, {span + 60, 0.2f, span + 60}, {0.11f, 0.11f, 0.12f}, "Asphalt");
    Instance* buildings = dm.create("Folder", city, "Buildings");
    Instance* furniture = dm.create("Folder", city, "StreetFurniture");
    Instance* lamps = dm.create("Folder", city, "StreetLamps");
    Instance* cars = dm.create("Folder", city, "Cars");
    const vec3 neon_cols[] = {{1.0f, 0.2f, 0.6f}, {0.2f, 0.85f, 1.0f}, {0.7f, 0.3f, 1.0f}, {1.0f, 0.55f, 0.15f}};

    for (int bx = 0; bx < kBlocks; ++bx)
        for (int bz = 0; bz < kBlocks; ++bz) {
            const f32 cx = center(bx), cz = center(bz);
            // Sidewalk slab with its curb, over the asphalt.
            part(dm, streets, "Sidewalk", {cx, kCurb * 0.5f, cz}, {kLot, kCurb, kLot}, {0.42f, 0.41f, 0.40f},
                 "Concrete");
            // Buildings: 2 x 2 lots of 20 m; each holds an 18 x 18 m building assembled from a CC0 modular facade
            // kit (tools/blender/make_city_buildings.py), outer faces flush with the sidewalk.
            const f32 inner = kLot - 2.0f * kSidewalk, lot = inner * 0.5f;
            for (int lx = 0; lx < 2; ++lx)
                for (int lz = 0; lz < 2; ++lz) {
                    const f32 ox = cx + (lx == 0 ? -1.0f : 1.0f) * (lot - kBuilding * 0.5f);
                    const f32 oz = cz + (lz == 0 ? -1.0f : 1.0f) * (lot - kBuilding * 0.5f);
                    const f32 r = rnd();
                    const CityBuilding& kind = kCityBuildings[u32(r * f32(kCityBuildingCount)) % kCityBuildingCount];
                    Instance* bld = dm.create("MeshPart", buildings, "Building");
                    bld->set("MeshId",
                             Value::string(std::string("builtin:models/city/buildings/") + kind.model + ".gltf"));
                    bld->set("Size", Value::vector3({1, 1, 1}));
                    bld->set("Position", Value::vector3({ox, kCurb, oz}));
                    bld->set("Orientation", Value::vector3({0, 90.0f * f32(u32(rnd() * 4.0f) % 4), 0}));
                    bld->set("Anchored", Value::boolean(true));
                    bld->set("CanCollide", Value::boolean(false));
                    // Collision: an invisible box over the footprint.
                    Instance* col = part(dm, bld, "Collision", {ox, kCurb + kind.height * 0.5f, oz},
                                         {kBuilding, kind.height, kBuilding}, {0.5f, 0.5f, 0.5f}, "Concrete");
                    col->set("Transparency", Value::number(1.0));
                    col->set("CastShadow", Value::boolean(false));
                    // Street-facing faces: neon signs and awnings over some shopfronts.
                    const vec3 nx{lx == 0 ? -1.0f : 1.0f, 0, 0}, nz{0, 0, lz == 0 ? -1.0f : 1.0f};
                    for (int face = 0; face < 2; ++face) {
                        const vec3 n = face == 0 ? nx : nz;
                        const vec3 fc = vec3{ox, kCurb, oz} + n * (kBuilding * 0.5f);
                        const vec3 t = face == 0 ? vec3{0, 0, 1} : vec3{1, 0, 0};
                        for (int k = 0; k < 6; ++k) {
                            const vec3 at = fc + t * ((f32(k) - 2.5f) * 3.0f);
                            const f32 q = rnd();
                            if (q < 0.2f) {
                                // Blade sign: two boards back to back, perpendicular to the facade.
                                static const char* const kNeon[] = {
                                    "neon_sign_open", "neon_sign_bar",   "neon_sign_hotel", "neon_sign_ramen",
                                    "neon_sign_24h",  "neon_sign_pizza", "neon_sign_cafe",  "neon_sign_liquor"};
                                const u32 pick = u32(rnd() * 8.0f) % 8;
                                const vec3 c = at + n * 0.95f + vec3{0, 3.9f, 0};
                                prop(furniture, "NeonSign", kNeon[pick], c, yaw_to(t));
                                prop(furniture, "NeonSign", kNeon[pick], c, yaw_to(-t));
                                light_at(furniture, c + n * 0.2f, neon_cols[pick % 4], 0.8f, 5.0f);
                            } else if (q < 0.38f) {
                                prop(furniture, "Awning", "awning", at + n * 0.02f + vec3{0, 2.95f, 0}, yaw_to(n));
                            }
                            // Shop light spilling onto the wet sidewalk.
                            // Shop window light: out of the glass and down onto the wet sidewalk, none back into the
                            // wall.
                            if (q < 0.55f)
                                spot_at(furniture, at + n * 0.3f + vec3{0, 2.3f, 0}, {-35.0f, yaw_to(n), 0.0f},
                                        rnd() < 0.5f ? vec3{1.0f, 0.84f, 0.64f} : vec3{0.86f, 0.93f, 1.0f}, 2.0f, 8.0f,
                                        130.0f, 0.0f, false);
                        }
                    }
                }
            // Sidewalk furniture and street lamps along the four curbs of this block.
            for (int side = 0; side < 4; ++side) {
                const vec3 n = side == 0   ? vec3{1, 0, 0}
                               : side == 1 ? vec3{-1, 0, 0}
                               : side == 2 ? vec3{0, 0, 1}
                                           : vec3{0, 0, -1};
                const vec3 t{std::fabs(n.z), 0, std::fabs(n.x)};
                const vec3 curb = vec3{cx, kCurb, cz} + n * (kLot * 0.5f - 0.6f);
                int slot = 0;
                for (f32 a = -kLot * 0.5f + 6.0f; a <= kLot * 0.5f - 6.0f; a += 11.0f, ++slot) {
                    const vec3 p = curb + t * a;
                    if (slot % 2 == 0) {
                        // Cast-iron lamp post (CC0, Poly Haven): warm lantern 4 m up.
                        prop(lamps, "StreetLamp", "lamp_classic", p, yaw_to(n));
                        // The lantern lights downwards and sideways; its cap lets only a little light upwards.
                        spot_at(lamps, p + vec3{0, 3.9f, 0}, {-90.0f, 0.0f, 0.0f}, {1.0f, 0.76f, 0.5f}, 14.0f, 22.0f,
                                172.0f, 0.07f, true);
                    } else {
                        const f32 r = rnd();
                        const char* what = r < 0.12f   ? "trash_bin"
                                           : r < 0.2f  ? "trash_bin_rusty"
                                           : r < 0.3f  ? "fire_hydrant"
                                           : r < 0.4f  ? "bench"
                                           : r < 0.5f  ? "parking_meter"
                                           : r < 0.58f ? "bollard"
                                           : r < 0.66f ? "utility_box"
                                           : r < 0.72f ? "planter"
                                                       : nullptr;
                        if (what) prop(furniture, what, what, p - n * 0.2f, yaw_to(-n));
                    }
                }
                // Parked cars along this curb, nose along the street.
                static const char* const kCars[] = {"car_sedan",     "car_sedan_black", "car_sedan_silver",
                                                    "car_hatch",     "car_hatch_white", "car_suv",
                                                    "car_suv_green", "car_taxi",        "covered_car"};
                for (f32 a = -kLot * 0.5f + 5.0f; a <= kLot * 0.5f - 5.0f; a += 5.8f) {
                    if (rnd() < 0.45f) continue;
                    const vec3 p = vec3{cx, 0.0f, cz} + n * (kLot * 0.5f + 1.3f) + t * a;
                    const char* model = kCars[u32(rnd() * 9.0f) % 9];
                    prop(cars, "Car", model, p, yaw_to(rnd() < 0.5f ? t : -t));
                }
            }
            // Alleys between the four buildings: crates, boxes, an old tyre, a barrel stove, a security light.
            static const char* const kClutter[] = {"cardboard_box", "wooden_crate", "plastic_crate",
                                                   "tyre",          "barrel_stove", "trash_bin_rusty"};
            for (int k = 0; k < 7; ++k) {
                const bool along_x = k % 2 == 0;
                const f32 off = (rnd() - 0.5f) * (kLot - 2.0f * kSidewalk - 4.0f);
                const f32 side = (rnd() < 0.5f ? -1.0f : 1.0f) * 1.3f;
                const vec3 p = along_x ? vec3{cx + off, kCurb, cz + side} : vec3{cx + side, kCurb, cz + off};
                prop(furniture, "Clutter", kClutter[u32(rnd() * 6.0f) % 6], p, rnd() * 360.0f);
            }
            {
                prop(furniture, "SecurityLight", "security_light", {cx + 1.85f, kCurb + 3.2f, cz}, 90.0f);
                spot_at(furniture, {cx + 1.6f, kCurb + 3.05f, cz}, {-60.0f, 90.0f, 0.0f}, {0.86f, 0.93f, 1.0f}, 4.0f,
                        12.0f, 110.0f, 0.04f, true);
            }
        }
    // Intersections: lane markings, crosswalks, traffic lights, steam from manholes.
    for (int ix = 0; ix < kBlocks - 1; ++ix)
        for (int iz = 0; iz < kBlocks - 1; ++iz) {
            const f32 x = center(ix) + kPitch * 0.5f, z = center(iz) + kPitch * 0.5f;
            for (int k = 0; k < 7; ++k) {
                const f32 o = (f32(k) - 3.0f) * 1.3f;
                for (int side = 0; side < 4; ++side) {
                    const f32 d = kStreet * 0.5f + 1.6f;
                    const vec3 c = side == 0   ? vec3{x + o, 0.003f, z + d}
                                   : side == 1 ? vec3{x + o, 0.003f, z - d}
                                   : side == 2 ? vec3{x + d, 0.003f, z + o}
                                               : vec3{x - d, 0.003f, z + o};
                    const vec3 sz = side < 2 ? vec3{0.6f, 0.01f, 2.6f} : vec3{2.6f, 0.01f, 0.6f};
                    part(dm, streets, "Crosswalk", c, sz, {0.85f, 0.85f, 0.82f}, "SmoothPlastic");
                }
            }
            for (int c = 0; c < 4; ++c) {
                const f32 sx = (c & 1) ? 1.0f : -1.0f, sz = (c & 2) ? 1.0f : -1.0f;
                const vec3 p{x + sx * (kStreet * 0.5f + 0.5f), kCurb, z + sz * (kStreet * 0.5f + 0.5f)};
                const bool ns = (c == 0 || c == 3);
                const vec3 face = ns ? vec3{0, 0, -sz} : vec3{-sx, 0, 0};
                prop(furniture, "TrafficLight", ns ? "traffic_light_red" : "traffic_light_green", p, yaw_to(face));
                light_at(furniture, p + vec3{0, 3.6f, 0} + face * 0.5f,
                         ns ? vec3{1, 0.12f, 0.08f} : vec3{0.2f, 1, 0.45f}, 1.2f, 6.0f);
            }
            if (rnd() < 0.5f) {
                Instance* mh = prop(furniture, "Manhole", "manhole", {x + 3.0f, 0.0f, z - 4.0f}, 0.0f);
                Instance* steam = dm.create("ParticleEmitter", mh, "Steam");
                steam->set("Texture", Value::string("builtin:smoke"));
                steam->set("Rate", Value::number(8.0));
                steam->set("Lifetime", Value::number(3.5));
                steam->set("Speed", Value::number(1.2));
                steam->set("SpreadAngle", Value::number(25.0));
                steam->set("Size", Value::number(0.8));
                steam->set("SizeEnd", Value::number(3.5));
                steam->set("Color", Value::color3({0.75f, 0.75f, 0.78f}));
                steam->set("ColorEnd", Value::color3({0.6f, 0.6f, 0.65f}));
                steam->set("Transparency", Value::number(0.55));
                steam->set("TransparencyEnd", Value::number(1.0));
                steam->set("Acceleration", Value::vector3({0.4f, 0.6f, 0.0f}));
                steam->set("RotSpeed", Value::number(25.0));
                script(dm, mh, "Script", "Steam", kSteam);
            }
        }
    // Dashed centre lines along every street.
    for (int i = 0; i < kBlocks - 1; ++i) {
        const f32 c = center(i) + kPitch * 0.5f;
        for (f32 a = -span * 0.5f; a < span * 0.5f; a += 9.0f) {
            bool crossing = false;
            for (int j = 0; j < kBlocks - 1; ++j)
                if (std::fabs(a - (center(j) + kPitch * 0.5f)) < kStreet * 0.5f + 3.5f) crossing = true;
            if (crossing) continue;
            part(dm, streets, "LaneMark", {c, 0.003f, a}, {0.14f, 0.01f, 3.0f}, {0.85f, 0.75f, 0.3f}, "SmoothPlastic");
            part(dm, streets, "LaneMark", {a, 0.003f, c}, {3.0f, 0.01f, 0.14f}, {0.85f, 0.75f, 0.3f}, "SmoothPlastic");
        }
    }
    // Spawn on a sidewalk corner.
    part(dm, ws, "SpawnLocation", {center(2) + kLot * 0.5f - 1.5f, kCurb + 0.02f, center(2) + kLot * 0.5f - 1.5f},
         {2, 0.04f, 2}, {0.42f, 0.41f, 0.40f}, "Concrete", "SpawnLocation");
    sound(dm, ws, "Rain", "builtin:rain_loop", 0.55f, true, true);
    // Night, heavy overcast, rain, haze that catches the lamps.
    Instance* l = dm.service("Lighting");
    l->set("ClockTime", Value::number(23.0));
    l->set("CloudCover", Value::number(0.95));
    l->set("SunBrightness", Value::number(0.0));
    l->set("EnvironmentDiffuseScale", Value::number(0.05));
    l->set("IndoorAmbient", Value::color3({0.6f, 0.66f, 0.78f})); // cool bounce from wet streets and clouds
    l->set("IndoorAmbientBrightness", Value::number(0.03));
    l->set("ExposureCompensation", Value::number(2.9));
    l->set("FogDensity", Value::number(0.003));
    l->set("FogHeightFalloff", Value::number(0.015));
    l->set("Saturation", Value::number(1.1));
    l->set("Contrast", Value::number(1.08));
    // Neutral blue-grey haze: the lamps tint it locally, the distance stays cold (no sepia wash).
    Instance* atmo = dm.create("Atmosphere", l, "Atmosphere");
    atmo->set("Color", Value::color3({0.7f, 0.77f, 0.9f}));
    atmo->set("Density", Value::number(0.0));
    Instance* rain = dm.create("Rain", l, "Rain");
    rain->set("Intensity", Value::number(0.85));
    rain->set("Wetness", Value::number(0.92));
    rain->set("Puddles", Value::number(0.55));
    Instance* sky = dm.create("Sky", l, "Sky");
    sky->set("StarCount", Value::number(0.0));
    sky->set("MoonVisible", Value::boolean(false));
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
    {"city_rain", "Rainy City", "A city block grid at night in the rain: wet streets, neon, street lamps, lit windows.",
     "Environment"},
    {"backrooms", "Backrooms", "Endless yellow rooms, damp carpet and buzzing fluorescent lights.", "Environment"},
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
    else if (id == "city_rain")
        build_city_rain(dm);
    else if (id == "backrooms")
        build_backrooms(dm);
    else if (id == "meadow")
        build_meadow(dm);
    else
        return false;
    return true;
}

void build_baseplate(DataModel& model) { build_template("baseplate", model); }

} // namespace tefra::studio
