#pragma once

// Native host for the C# scripting layer (spec M3, ADR-013). Loads the .NET 8 runtime with hostfxr at run time (no
// nethost link, so it works the same with MinGW on Windows), hands the managed assembly the native function table
// and drives the script lifecycle from the engine loop.
//
// Usage (Studio or tefra_run):
//   scripting::ScriptHost host;
//   std::string err;
//   if (!host.init(&err)) { /* no .NET: continue without scripts, show err in the Output */ }
//   host.set_log_callback([](LogLevel lvl, std::string_view msg) { ... });
//   host.start(model);                 // compiles and runs Start() of the enabled Scripts
//   while (running) host.step(dt);     // Heartbeat, tasks, Update()
//   host.stop();                       // drops the scripts; the DataModel keeps whatever the scripts changed
//
// Threading: everything runs on the thread that calls init(). Exceptions never cross into this code: managed code
// catches them, reports them through the log callback and disables the script that threw.

#include "tefra/core/log.h"
#include "tefra/core/types.h"
#include "tefra/scripting/abi.h"
#include "tefra/world/datamodel.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tefra::scripting {

// Log sink for Debug.Log, print and script errors. Default: the engine log (stderr).
using LogCallback = std::function<void(LogLevel level, std::string_view message)>;

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

    // Starts a play session on `model`: compiles every enabled Script under Workspace and ServerScriptService and
    // calls Start() on each. Scripts that fail to compile or throw in Start are disabled and reported; start() still
    // returns true. Returns false only if the runtime itself failed (then the session is not running).
    bool start(world::DataModel& model);

    // Advances one frame: scheduled tasks, Heartbeat, Update(dt) on every running script, then the queued
    // change notifications. No-op when not running.
    void step(f32 dt);

    // Ends the play session. Script state is discarded; the DataModel is left as the scripts left it.
    void stop();

    bool running() const { return running_; }

  private:
    void flush_events();

    LogCallback log_;
    bool running_ = false;
    world::DataModel* model_ = nullptr;
    usize listener_ = 0;
    std::vector<TefraEvent> events_; // queued by DataModel listeners, delivered to managed code once per step
};

} // namespace tefra::scripting
