#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/model/ProjectModel.h"

namespace vo {

// Returns true when a path (relative to the root) is occupied on disk by
// something that is NOT the current file of a project clip — i.e. a name that
// can never be used without overwriting (spec §21).
using DiskOccupancy = std::function<bool(const std::string& relPath)>;

struct NamingContext {
    std::string folder;
    int number = 1;
    std::string original; // original filename without extension
    std::optional<std::string> camera;
    std::optional<std::string> resolution;
    std::optional<std::string> date;
};

// Naming templates (spec §20). Supported variables:
//   {folder} {number} {number:N} {original} {camera} {resolution} {date}
class NamingEngine {
public:
    static std::vector<std::string> supportedVariables();
    // Returns an error message for unknown variables / unbalanced braces.
    static std::optional<std::string> validateTemplate(const std::string& tmpl);
    static bool usesNumber(const std::string& tmpl);
    static std::string render(const std::string& tmpl, const NamingContext& ctx); // sanitized
    static NamingContext contextFor(const ProjectModel& m, const ClipId& clip, int number);

    // Re-titles every clip without a title override in the given folders.
    // Numbering is sequential per folder in a deterministic order (recording
    // time, then original filename, then id); names already used in the
    // folder or occupied on disk are skipped. An empty template keeps titles.
    static void applyNaming(ProjectModel& m, const std::string& tmpl, const std::vector<FolderId>& folders,
                            const DiskOccupancy& occupied);

    // Deterministic clip ordering used for numbering.
    static std::vector<ClipId> numberingOrder(const ProjectModel& m, std::vector<ClipId> clips);
};

} // namespace vo
