#pragma once

#include <string>
#include <vector>

#include "core/model/ProjectModel.h"

namespace vo {

enum class ChangeType { CreateFolder, RenameFolder, MoveFolder, MoveClip, RenameClip };
enum class ChangeOrigin { Automatic, Manual };

std::string toString(ChangeType t);
std::string toString(ChangeOrigin o);

// One explicit filesystem operation that Apply would perform (spec §24).
struct Change {
    ChangeType type;
    ChangeOrigin origin = ChangeOrigin::Automatic;
    ClipId clipId;     // clip changes
    FolderId folderId; // folder changes
    std::string sourcePath;      // relative to root ("" for CreateFolder)
    std::string destinationPath; // relative to root
    std::string oldName;
    std::string newName;
    bool moves = false;   // parent directory changes
    bool renames = false; // final path component changes
};

struct ChangeSet {
    std::vector<Change> folderChanges; // parents before children (execution order)
    std::vector<Change> clipChanges;
    std::vector<ClipId> skippedMissing; // missing clips whose intended path differs

    bool empty() const { return folderChanges.empty() && clipChanges.empty(); }
    size_t size() const { return folderChanges.size() + clipChanges.size(); }
};

// Derives the pending operations by diffing the virtual hierarchy (intended
// paths) against the confirmed physical state. Because the change list is
// always derived from the model, Review shows exactly what Apply will do.
ChangeSet computeChanges(const ProjectModel& m);

// Pre-order folder traversal from the root with deterministic sibling order.
std::vector<FolderId> foldersTopDown(const ProjectModel& m);

} // namespace vo
