#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/changes/ChangeSet.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/PreflightValidator.h"
#include "core/model/ProjectModel.h"

namespace vo {

// Persistent operation journal (spec §52–§53). Each operation is recorded as
// PENDING before it runs; its result and the resulting model changes are then
// committed together, so after a crash the database never claims an operation
// succeeded unless it was confirmed.
class ApplyJournal {
public:
    virtual ~ApplyJournal() = default;
    virtual std::string beginBatch() = 0;
    virtual int64_t recordPending(const std::string& batchId, int seq, const std::string& type, const ClipId& clip,
                                  const FolderId& folder, const std::string& source, const std::string& destination) = 0;
    virtual void recordResult(int64_t opId, bool success, const std::string& error, const DeltaList& deltas) = 0;
    virtual void finishBatch(const std::string& batchId, const std::string& state) = 0;
};

struct PendingOperation {
    int64_t id = 0;
    std::string batchId;
    std::string type;
    ClipId clipId;
    FolderId folderId;
    std::string source;
    std::string destination;
};

struct ApplyOperationResult {
    std::string type; // CREATE_FOLDER, RENAME_FOLDER, MOVE_FOLDER, MOVE_CLIP, RENAME_CLIP, STAGE, REMOVE_EMPTY_DIRECTORY
    ClipId clipId;
    FolderId folderId;
    std::string source;
    std::string destination;
    bool success = false;
    std::string error;
    std::string description;
};

enum class ApplyState { Applied, PartiallyApplied, Failed, NothingToApply, Blocked };
std::string toString(ApplyState s);

struct ApplyResult {
    ApplyState state = ApplyState::NothingToApply;
    std::vector<ApplyOperationResult> operations;
    size_t succeeded = 0;
    size_t failed = 0;
    std::vector<std::string> removedDirectories;
    PreflightReport preflight;
};

// Executes the pending change set one operation at a time (spec §26–§27):
// directory renames/moves and creations top-down, then clip moves/renames.
// Partial execution is expected; every outcome is reported and the model is
// updated only for confirmed operations.
class ApplyEngine {
public:
    ApplyEngine(ProjectModel& model, FileSystem& fs, ApplyJournal& journal);

    // Caller is responsible for refresh + preflight; this executes whatever is
    // pending now.
    ApplyResult execute();

    // Reconciles operations left PENDING by an interrupted Apply against disk.
    static std::vector<ApplyOperationResult> recover(ProjectModel& model, FileSystem& fs, ApplyJournal& journal,
                                                     const std::vector<PendingOperation>& pending);

    // Rewrites confirmed physical paths after a directory moved on disk.
    static void relocatePhysicalPrefix(ProjectModel& model, const std::string& from, const std::string& to);

private:
    struct Op {
        std::string type;
        ClipId clipId;
        FolderId folderId;
        std::string source;
        std::string destination;
        std::string description;
    };

    bool run(const Op& op, const std::function<std::optional<FsError>()>& action,
             const std::function<void()>& onSuccess, bool allowExistingDirectory = false);
    std::string stagingName(const std::string& relPath) const;
    void applyFolders(const ChangeSet& changes);
    void applyClips();
    void cleanupDirectories();

    ProjectModel& m_;
    FileSystem& fs_;
    ApplyJournal& journal_;
    std::string batchId_;
    int seq_ = 0;
    ApplyResult result_;
    std::vector<std::string> vacatedDirectories_;
};

} // namespace vo
