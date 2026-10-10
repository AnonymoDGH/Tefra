#pragma once

// Editor session (spec M3 §1): the place being edited, the selection, undo history and play mode. Every editing
// command goes through here so the history and the "modified" flag stay right; the UI panels and the MCP tools
// call these functions instead of touching the DataModel directly.

#include "tefra/studio/history.h"
#include "tefra/world/datamodel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tefra::studio {

enum class PlayState : u8 { Edit, Playing, Paused };

// Systems that run in play mode (scripting, physics, characters, animation) attach here.
struct PlayHooks {
    std::string name;
    std::function<bool(world::DataModel&)> start; // false = failed (reported, play continues without it)
    std::function<void(world::DataModel&, f32 dt)> step;
    std::function<void()> stop;
};

class Session {
  public:
    Session();

    world::DataModel& model() { return *model_; }
    // Bumped whenever the DataModel object is replaced (new, open, stop): systems holding a reference rebuild.
    u64 generation() const { return generation_; }
    const world::DataModel& model() const { return *model_; }

    // ---- Files -----------------------------------------------------------------------------------------------
    void new_place(bool baseplate = true);
    bool open(const std::string& path, std::string* error = nullptr);
    bool save(const std::string& path = {}); // empty: current path
    const std::string& path() const { return path_; }
    bool modified() const { return model_->revision() != saved_revision_; }

    // ---- Selection ---------------------------------------------------------------------------------------------
    const std::vector<u64>& selection() const { return selection_; }
    world::Instance* primary() const; // last selected, nullptr if none / gone
    void select(u64 id, bool additive = false);
    void select_only(const std::vector<u64>& ids);
    void clear_selection() { selection_.clear(); }
    bool is_selected(u64 id) const;
    void prune_selection(); // drops ids that no longer exist

    // ---- Editing (recorded in the history; refused in play mode where noted) ------------------------------
    world::Instance* create(const std::string& class_name, world::Instance* parent, const std::string& name = {});
    bool destroy_selection();
    std::vector<u64> duplicate_selection();
    world::Instance* group_selection(); // into a new Model
    bool reparent(world::Instance* inst, world::Instance* parent);
    bool rename(world::Instance* inst, const std::string& name);
    // One-shot property change with history.
    bool set_property(world::Instance* inst, u32 prop, const world::Value& v, const char* label = nullptr);
    // Continuous edits (drags, gizmos): begin records the starting values, end pushes one action.
    void begin_edit(const char* label);
    void track(world::Instance* inst, u32 prop); // remember the "before" value (once per gesture)
    void set_live(world::Instance* inst, u32 prop, const world::Value& v); // apply without history
    void end_edit();
    bool undo();
    bool redo();
    History& history() { return history_; }

    // ---- Play mode ------------------------------------------------------------------------------------------
    PlayState play_state() const { return state_; }
    void add_hooks(PlayHooks hooks) { hooks_.push_back(std::move(hooks)); }
    void play();
    void pause(bool paused);
    void stop();
    void step(f32 dt); // called every frame; runs the hooks while playing
    f64 play_time() const { return play_time_; }

  private:
    void record(Action a);
    std::unique_ptr<world::DataModel> model_;
    u64 generation_ = 0;
    std::string path_;
    u64 saved_revision_ = 0;
    std::vector<u64> selection_;
    History history_;
    // Gesture in progress.
    bool editing_ = false;
    Action gesture_;
    // Play mode.
    PlayState state_ = PlayState::Edit;
    JsonValue snapshot_;
    std::vector<u64> snapshot_selection_;
    std::vector<PlayHooks> hooks_;
    std::vector<bool> hook_running_;
    f64 play_time_ = 0.0;
};

// Places the default "Baseplate" template into an empty model.
void build_baseplate(world::DataModel& model);

} // namespace tefra::studio
