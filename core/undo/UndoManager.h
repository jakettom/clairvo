#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/model/ProjectModel.h"

namespace vo {

// Session-only (not persisted) undo/redo of virtual edits (FR-CHANGE-004/005).
class UndoManager {
public:
    struct Entry {
        std::string description;
        DeltaList deltas;
    };

    // Records a completed command. Empty delta lists are ignored.
    void push(std::string description, DeltaList deltas);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::string undoDescription() const { return undo_.empty() ? "" : undo_.back().description; }
    std::string redoDescription() const { return redo_.empty() ? "" : redo_.back().description; }

    // Return false when there is nothing to undo/redo. The model records the
    // resulting state changes as new deltas, which the caller persists.
    bool undo(ProjectModel& model);
    bool redo(ProjectModel& model);

    // Apply and import change the physical baseline, so history is dropped.
    void clear();

    size_t depth() const { return undo_.size(); }

private:
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
    static constexpr size_t kMaxDepth = 200;
};

} // namespace vo
