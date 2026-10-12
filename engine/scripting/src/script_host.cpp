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

TefraEvent make_event(u32 kind, u64 id) {
    TefraEvent e{};
    e.kind = kind;
    e.id = id;
    return e;
}

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

void ScriptHost::set_input(const InputState* input) { detail::set_input(input); }

void ScriptHost::set_physics(PhysicsHooks hooks) {
    hooks_ = std::move(hooks);
    detail::set_physics_hooks(&hooks_);
}

bool ScriptHost::start(world::DataModel& model) {
    if (!g_ready) return false;
    if (running_) stop();

    model_ = &model;
    events_.clear();
    strings_.clear();
    world::WorldListener listener;
    listener.added = [this](world::Instance& inst) { events_.push_back(make_event(TEFRA_EVENT_ADDED, inst.id())); };
    listener.reparented = [this](world::Instance& inst, world::Instance* old_parent) {
        TefraEvent e = make_event(TEFRA_EVENT_REPARENTED, inst.id());
        e.other = old_parent ? old_parent->id() : 0;
        events_.push_back(e);
    };
    listener.removing = [this](world::Instance& inst) {
        events_.push_back(make_event(TEFRA_EVENT_REMOVING, inst.id()));
    };
    listener.changed = [this](world::Instance& inst, u32 prop) {
        TefraEvent e = make_event(TEFRA_EVENT_CHANGED, inst.id());
        e.prop = prop;
        events_.push_back(e);
    };
    listener.event = [this](const world::GameEvent& ev) {
        if (!ev.source) return;
        strings_.emplace_back(ev.name);
        const char* name = strings_.back().c_str();
        const char* text = "";
        if (!ev.text.empty()) {
            strings_.emplace_back(ev.text);
            text = strings_.back().c_str();
        } else {
            strings_.emplace_back();
            text = strings_.back().c_str();
        }
        TefraEvent e = make_event(TEFRA_EVENT_SIGNAL, ev.source->id());
        e.other = ev.other ? ev.other->id() : 0;
        e.number = ev.number;
        e.name = name;
        e.text = text;
        events_.push_back(e);
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
    // Structure first: a respawn clones the GUI and the character outside a step, and the scripts they bring must be
    // synced before the signals queued with them (CharacterAdded) are dispatched, or they miss those signals.
    flush_structural();
    g_managed.step(dt);
    flush_events();
}

void ScriptHost::flush_structural() {
    std::vector<TefraEvent> structural;
    std::vector<TefraEvent> rest;
    for (const TefraEvent& e : events_) {
        const bool is_structure =
            e.kind == TEFRA_EVENT_ADDED || e.kind == TEFRA_EVENT_REPARENTED || e.kind == TEFRA_EVENT_REMOVING;
        (is_structure ? structural : rest).push_back(e);
    }
    if (structural.empty()) return;
    events_.swap(rest); // signals and changes stay queued, in order, for flush_events()
    g_managed.dispatch(structural.data(), static_cast<u32>(structural.size()));
}

void ScriptHost::stop() {
    if (!running_) return;
    running_ = false;
    g_managed.stop();
    if (model_) model_->remove_listener(listener_);
    model_ = nullptr;
    events_.clear();
    strings_.clear();
    detail::set_session(nullptr, nullptr);
}

void ScriptHost::flush_events() {
    if (events_.empty()) return;
    // Managed handlers may change the model; those changes are queued for the next batch. The strings of this batch
    // must outlive the dispatch call, so they travel with the events.
    std::vector<TefraEvent> batch;
    std::deque<std::string> texts;
    batch.swap(events_);
    texts.swap(strings_);
    g_managed.dispatch(batch.data(), static_cast<u32>(batch.size()));
}

} // namespace tefra::scripting
