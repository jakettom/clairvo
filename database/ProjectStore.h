#pragma once

#include <string>
#include <vector>

#include "core/filesystem/ApplyEngine.h"
#include "core/model/ProjectModel.h"
#include "database/Sqlite.h"

namespace vo {

// SQLite persistence for one project. The `<Name>.project` file in the
// footage root *is* this database (spec §5).
class ProjectStore : public ApplyJournal {
public:
    // Creates a new project file; throws if it already exists.
    void create(const std::string& projectFile, const ProjectInfo& info, const Folder& root,
                const OrganizationConfig& config);
    void open(const std::string& projectFile);
    void close() { db_.close(); }

    // Loads the full project state into an empty model (no deltas recorded).
    void load(ProjectModel& model);

    // Writes the current model state of every entity touched by `deltas`, in a
    // single transaction.
    void persist(const ProjectModel& model, const DeltaList& deltas);
    void updateProjectRow(const ProjectInfo& info);

    // Journal of interrupted Apply operations.
    std::vector<PendingOperation> pendingOperations();

    // ApplyJournal
    std::string beginBatch() override;
    int64_t recordPending(const std::string& batchId, int seq, const std::string& type, const ClipId& clip,
                          const FolderId& folder, const std::string& source, const std::string& destination) override;
    void recordResult(int64_t opId, bool success, const std::string& error, const DeltaList& deltas) override;
    void finishBatch(const std::string& batchId, const std::string& state) override;

    // The journal needs the model to persist deltas alongside results.
    void attachModel(const ProjectModel* model) { model_ = model; }

    db::Database& database() { return db_; }

private:
    void writeDeltas(const ProjectModel& model, const DeltaList& deltas); // inside a transaction
    void writeFolder(const ProjectModel& model, const FolderId& id);
    void writeClip(const ProjectModel& model, const ClipId& id);
    void writeTag(const ProjectModel& model, const TagId& id);
    void writeClipTags(const ProjectModel& model, const ClipId& id);
    void writeClipMetadata(const ProjectModel& model, const ClipId& id);
    void writeConfig(const ProjectModel& model);

    db::Database db_;
    std::string projectId_;
    const ProjectModel* model_ = nullptr;
};

} // namespace vo
