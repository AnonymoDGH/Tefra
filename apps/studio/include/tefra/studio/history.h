#pragma once

// Undo/redo for Tefra Studio (spec M3 §5). An Action groups the Ops made together (a drag, a gizmo gesture, a
// duplicate). Ops refer to instances by id, so they stay valid across undo/redo while the model is not replaced.

#include "tefra/core/json_value.h"
#include "tefra/world/datamodel.h"

#include <string>
#include <vector>

namespace tefra::studio {

// A reference held by another instance that a Destroy cleared; restored when the Destroy is undone.
struct RefFix {
    u64 holder = 0;
    u32 prop = 0;
    u64 ref = 0;
};

struct Op {
    enum class Kind : u8 { SetProperty, Create, Destroy, Reparent };
    Kind kind = Kind::SetProperty;
    u64 id = 0;         // target instance
    u64 parent = 0;     // Create/Destroy: parent at the time; Reparent: new parent (0 = detached)
    u64 old_parent = 0; // Reparent: previous parent (0 = detached)
    u32 prop = 0;       // SetProperty: global property index
    world::Value before;
    world::Value after;
    JsonValue snapshot;       // Create: subtree after creation; Destroy: subtree before removal
    std::vector<RefFix> refs; // Destroy: references into the removed subtree
};

struct Action {
    std::string label;
    std::vector<Op> ops;
};

// Applies one op forwards (redo) or backwards (undo). Returns false if the ids it needs no longer exist.
bool apply_op(world::DataModel& model, const Op& op, bool forward);

// Captures a Create op from an instance that now exists (snapshot of its subtree).
Op capture_create(const world::DataModel& model, const world::Instance& inst);
// Captures a Destroy op for an instance that is about to be removed (snapshot and references into it).
Op capture_destroy(const world::DataModel& model, const world::Instance& inst);

class History {
  public:
    // Records a finished action. Clears the redo stack.
    void push(Action action);
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    // Undo/redo the newest action. Returns false when there is nothing to do or an op could not be applied.
    bool undo(world::DataModel& model, std::string* label = nullptr);
    bool redo(world::DataModel& model, std::string* label = nullptr);
    const std::string& undo_label() const;
    const std::string& redo_label() const;
    void clear();
    usize undo_depth() const { return undo_.size(); }
    usize redo_depth() const { return redo_.size(); }

  private:
    std::vector<Action> undo_;
    std::vector<Action> redo_;
};

} // namespace tefra::studio
