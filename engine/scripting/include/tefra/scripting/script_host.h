#pragma once

// Native host for the C# scripting layer (spec M3, ADR-013; M3b: events, input, raycasts). Loads the .NET 8 runtime
// with hostfxr at run time (no nethost link, so it works the same with MinGW on Windows), hands the managed assembly
// the native function table and drives the script lifecycle from the engine loop.
//
// Usage (Studio, tefra_run or the player):
//   scripting::ScriptHost host;
//   std::string err;
//   if (!host.init(&err)) { /* no .NET: continue without scripts, show err in the Output */ }
//   host.set_log_callback([](LogLevel lvl, std::string_view msg) { ... });
//   host.set_input(&input);            // optional: UserInputService reads this snapshot
//   host.set_raycaster(raycast_fn);    // optional: Workspace:Raycast
//   host.start(model);                 // compiles and runs Start() of the scripts that should run
//   while (running) host.step(dt);     // structure changes, Heartbeat, tasks, Update(), then the queued events
//   host.stop();                       // drops the scripts; the DataModel keeps whatever the scripts changed
//
// Threading: everything runs on the thread that calls init(). Exceptions never cross into this code: managed code
// catches them, reports them through the log callback and disables the script that threw.

#include "tefra/core/input.h"
#include "tefra/core/log.h"
#include "tefra/core/math.h"
#include "tefra/core/types.h"
#include "tefra/scripting/abi.h"
#include "tefra/world/datamodel.h"

#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::scripting {

// Log sink for Debug.Log, print and script errors. Default: the engine log (stderr).
using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

// Answer of a raycast requested by a script (Workspace:Raycast).
struct RayHit {
    world::Instance* instance = nullptr;
    vec3 position{};
    vec3 normal{};
    f32 distance = 0.0f;
};

// Returns true and fills `out` for the closest solid part along the ray (sensors excluded), false on a miss.
using RaycastFn = std::function<bool(vec3 origin, vec3 direction, f32 max_distance, RayHit& out)>;

class ScriptHost {
  public:
    ScriptHost() = default;
    ~ScriptHost(); // stops a running session
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Locates the .NET runtime and the managed assembly, starts the runtime and initializes the managed side.
    // `managed_dir` holds Tefra.Engine.dll and its runtimeconfig; empty = "managed" next to the executable.
    // Returns false with a message in *err when scripting is unavailable (no .NET 8, missing assembly); the engine
    // must keep working in that case. Calling init() again after success returns true.
    bool init(std::string* err, std::string_view managed_dir = {});

    // True after a successful init().
    bool available() const;

    void set_log_callback(LogCallback callback);

    // The input snapshot read by UserInputService (IsKeyDown, MouseLocation). The pointer must stay valid while a
    // session runs; nullptr = no input (every key up). Safe to call before or during a session.
    void set_input(const InputState* input);

    // The function behind Workspace:Raycast. nullptr = no raycasts (they miss). Safe to call during a session.
    void set_raycaster(RaycastFn fn);

    // Starts a play session on `model`: compiles every script that should run and calls Start() on each.
    //   Script:      under Workspace or ServerScriptService.
    //   LocalScript: under a Player (PlayerGui, PlayerScripts) or under a Player's Character.
    //   ModuleScript: compiled once, visible to every script (see ScriptEngine notes in the M3b report).
    // Scripts that fail to compile or throw in Start are disabled and reported; start() still returns true. Returns
    // false only if the runtime itself failed (then the session is not running).
    bool start(world::DataModel& model);

    // Advances one frame: applies script structure changes (scripts added, moved or removed), scheduled tasks,
    // Heartbeat, Update(dt) on every running script, then the queued events. No-op when not running.
    void step(f32 dt);

    // Ends the play session. Script state is discarded; the DataModel is left as the scripts left it.
    void stop();

    bool running() const { return running_; }

  private:
    void flush_structural();
    void flush_events();

    LogCallback log_;
    bool running_ = false;
    world::DataModel* model_ = nullptr;
    usize listener_ = 0;
    std::vector<TefraEvent> events_;  // queued by DataModel listeners, delivered to managed code once per step
    std::deque<std::string> strings_; // names and texts of queued signals (deque: pointers stay valid on push)
    RaycastFn raycaster_;             // read by the native raycast entry while a session runs
};

} // namespace tefra::scripting
