#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/changes/ChangeSet.h"
#include "core/filesystem/ApplyEngine.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/MissingFileDetector.h"
#include "core/filesystem/PreflightValidator.h"
#include "core/model/ProjectModel.h"
#include "core/organization/NamingEngine.h"
#include "core/undo/UndoManager.h"
#include "database/ProjectStore.h"
#include "media/extraction/MetadataExtractor.h"

namespace vo {

// The open project: owns the authoritative model, its SQLite store, the undo
// history and the filesystem/metadata services. Every mutation flows through
// here so it is persisted (write-through) and announced to the UI.
class ProjectSession {
public:
    struct Dependencies {
        std::shared_ptr<FileSystem> fileSystem;
        std::shared_ptr<media::MetadataExtractor> extractor;
    };
    using EventSink = std::function<void(const std::string& type, const nlohmann::json& payload)>;

    static std::unique_ptr<ProjectSession> create(const std::string& rootPath, const std::string& projectName,
                                                  Dependencies deps);
    static std::unique_ptr<ProjectSession> open(const std::string& projectFile, Dependencies deps);

    ProjectSession(const ProjectSession&) = delete;
    ProjectSession& operator=(const ProjectSession&) = delete;
    ~ProjectSession();

    std::recursive_mutex& mutex() const { return mutex_; }
    const ProjectModel& model() const { return model_; }
    const UndoManager& undoManager() const { return undo_; }
    FileSystem& fileSystem() { return *deps_.fileSystem; }
    media::MetadataExtractor& extractor() { return *deps_.extractor; }

    // ---- Undoable virtual edits -------------------------------------------
    // Runs `fn` as one command: on success its deltas are persisted and pushed
    // onto the undo stack; on any exception the model is rolled back.
    template <class F>
    auto edit(const std::string& description, F&& fn) -> decltype(fn(std::declval<ProjectModel&>()));

    bool undo();
    bool redo();

    void organize(const OrganizationConfig& config);
    void applyNaming(const std::string& namingTemplate);
    void resetToAutomatic(const std::vector<ClipId>& clips);
    void resetOrganization();
    DiskOccupancy diskOccupancy() const;

    // ---- Physical state -----------------------------------------------------
    RefreshResult refresh();
    ChangeSet pendingChanges() const;
    PreflightReport preflight();
    using ApplyProgress = std::function<void(size_t done, size_t total)>;
    ApplyResult apply();
    const std::vector<ApplyOperationResult>& recoveryReport() const { return recoveryReport_; }

    // ---- Import support (used by ImportService) -------------------------------
    // Persists non-undoable mutations (import, refresh) and drops undo history
    // when the physical baseline changed.
    void commitNonUndoable(bool clearUndoHistory);
    ProjectModel& mutableModel() { return model_; }

    std::atomic<bool>& busy() { return busy_; }
    void ensureNotBusy() const;

    void setEventSink(EventSink sink);
    void emit(const std::string& type, const nlohmann::json& payload) const;

private:
    ProjectSession(Dependencies deps);
    void commitEdit(const std::string& description);
    void rollbackEdit();

    Dependencies deps_;
    mutable std::recursive_mutex mutex_;
    ProjectModel model_;
    ProjectStore store_;
    UndoManager undo_;
    std::vector<ApplyOperationResult> recoveryReport_;
    std::atomic<bool> busy_{false};
    EventSink sink_;
    mutable std::mutex sinkMutex_;
};

template <class F>
auto ProjectSession::edit(const std::string& description, F&& fn) -> decltype(fn(std::declval<ProjectModel&>())) {
    std::lock_guard lock(mutex_);
    ensureNotBusy();
    using R = decltype(fn(model_));
    try {
        if constexpr (std::is_void_v<R>) {
            fn(model_);
            commitEdit(description);
        } else {
            R result = fn(model_);
            commitEdit(description);
            return result;
        }
    } catch (...) {
        rollbackEdit();
        throw;
    }
}

} // namespace vo
