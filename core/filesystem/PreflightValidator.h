#pragma once

#include <string>
#include <vector>

#include "core/changes/ChangeSet.h"
#include "core/filesystem/FileSystem.h"
#include "core/model/ProjectModel.h"

namespace vo {

enum class IssueSeverity { Error, Warning, Info };
std::string toString(IssueSeverity s);

struct PreflightIssue {
    IssueSeverity severity = IssueSeverity::Error;
    std::string message;
    std::string path;
    ClipId clipId;
    FolderId folderId;
};

struct PreflightCheck {
    std::string description;
    bool passed = true;
};

struct PreflightReport {
    bool ok = true; // no blocking errors
    std::vector<PreflightCheck> checks;
    std::vector<PreflightIssue> issues;
    size_t folderCreates = 0;
    size_t folderMoves = 0;
    size_t clipMoves = 0;
    size_t clipRenames = 0;
    size_t errorCount() const;
    size_t warningCount() const;
};

// Validates a change set against the real filesystem before anything is
// touched (spec §50–§51, FR-REVIEW-005). Apply is blocked while errors exist.
PreflightReport runPreflight(const ProjectModel& m, const ChangeSet& changes, FileSystem& fs);

} // namespace vo
