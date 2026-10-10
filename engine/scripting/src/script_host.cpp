#include "tefra/scripting/script_host.h"

#include "dotnet_host.h"
#include "script_session.h"

#include "tefra/core/fs.h"

#include <filesystem>

namespace tefra::scripting {
namespace {

// One runtime per process: hostfxr starts CoreCLR once and the managed side is initialized once.
TefraManagedApi g_managed{};
bool g_ready = false;

} // namespace

ScriptHost::~ScriptHost() { stop(); }

bool ScriptHost::init(std::string* err, std::string_view managed_dir) {
    if (g_ready) return true;
    const std::filesystem::path dir = managed_dir.empty() ? std::filesystem::path(exe_dir()) / "managed"
                                                          : std::filesystem::path(std::string(managed_dir));
    std::string message;
    if (!dotnet::load_managed(dir, detail::native_table(), &g_managed, &message)) {
        if (err) *err = message;
        return false;
    }
    g_ready = true;
    TEFRA_LOG_INFO("scripting: .NET runtime ready (managed: %s)", dir.string().c_str());
    return true;
}

bool ScriptHost::available() const { return g_ready; }

void ScriptHost::set_log_callback(LogCallback callback) { log_ = std::move(callback); }

bool ScriptHost::start(world::DataModel& model) {
    if (!g_ready) return false;
    if (running_) stop();

    model_ = &model;
    events_.clear();
    world::WorldListener listener;
    listener.removing = [this](world::Instance& inst) {
        events_.push_back(TefraEvent{TEFRA_EVENT_REMOVING, 0, inst.id()});
    };
    listener.changed = [this](world::Instance& inst, u32 prop) {
        events_.push_back(TefraEvent{TEFRA_EVENT_CHANGED, prop, inst.id()});
    };
    listener_ = model.add_listener(std::move(listener));

    detail::set_session(&model, &log_);
    running_ = true;
    if (g_managed.start() < 0) {
        stop();
        return false;
    }
    return true;
}

void ScriptHost::step(f32 dt) {
    if (!running_) return;
    g_managed.step(dt);
    flush_events();
}

void ScriptHost::stop() {
    if (!running_) return;
    running_ = false;
    g_managed.stop();
    if (model_) model_->remove_listener(listener_);
    model_ = nullptr;
    events_.clear();
    detail::set_session(nullptr, nullptr);
}

void ScriptHost::flush_events() {
    if (events_.empty()) return;
    std::vector<TefraEvent> batch;
    batch.swap(events_); // managed handlers may change properties; those go to the next batch
    g_managed.dispatch(batch.data(), static_cast<u32>(batch.size()));
}

} // namespace tefra::scripting
