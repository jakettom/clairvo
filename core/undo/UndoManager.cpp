#include "core/undo/UndoManager.h"

namespace vo {

void UndoManager::push(std::string description, DeltaList deltas) {
    if (deltas.empty()) return;
    undo_.push_back({std::move(description), std::move(deltas)});
    if (undo_.size() > kMaxDepth) undo_.erase(undo_.begin());
    redo_.clear();
}

bool UndoManager::undo(ProjectModel& model) {
    if (undo_.empty()) return false;
    Entry e = std::move(undo_.back());
    undo_.pop_back();
    model.revert(e.deltas);
    redo_.push_back(std::move(e));
    return true;
}

bool UndoManager::redo(ProjectModel& model) {
    if (redo_.empty()) return false;
    Entry e = std::move(redo_.back());
    redo_.pop_back();
    model.reapply(e.deltas);
    undo_.push_back(std::move(e));
    return true;
}

void UndoManager::clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace vo
