#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/model/ProjectModel.h"
#include "core/organization/NamingEngine.h"

namespace vo {

// Automatic organization (spec §17–§23, §60–§63). Deterministic given the same
// project state, metadata, configuration and overrides. Operates on the
// virtual model only; it never touches the filesystem (invariant 7).
class OrganizationEngine {
public:
    // Folder-name path (below the root) proposed for a clip by the criteria.
    static std::vector<std::string> automaticPlacement(const ProjectModel& m, const ClipId& clip,
                                                       const std::vector<MetadataCategory>& criteria);

    // Read-only proposal: clip -> proposed folder path, for every clip that
    // automatic organization is allowed to move.
    static std::map<ClipId, std::string> propose(const ProjectModel& m, const OrganizationConfig& config);

    // "Preview Organization": stores the config, re-places every clip without a
    // placement override, prunes emptied automatic/imported folders and applies
    // the naming template. Recorded as one undoable command by the caller.
    static void organize(ProjectModel& m, const OrganizationConfig& config, const DiskOccupancy& occupied);

    // Clears manual overrides and re-evaluates the clips with the current config.
    static void resetToAutomatic(ProjectModel& m, const std::vector<ClipId>& clips, const DiskOccupancy& occupied);

    // Discards all pending virtual changes: the hierarchy returns to the
    // physical layout on disk and all overrides are cleared.
    static void resetOrganization(ProjectModel& m);

    // Re-titles all clips without a title override using the config template.
    static void applyNaming(ProjectModel& m, const std::string& tmpl, const DiskOccupancy& occupied);

    // Returns the folder representing a physical directory, creating
    // IMPORTED folders for missing levels.
    static FolderId ensurePhysicalFolder(ProjectModel& m, const std::string& relDir);

    // Removes empty IMPORTED/AUTOMATIC folders (never MANUAL, never the root).
    static void pruneEmptyFolders(ProjectModel& m);

private:
    static FolderId ensureAutomaticPath(ProjectModel& m, const std::vector<std::string>& names);
    static FolderId targetFolder(ProjectModel& m, const ClipId& clip, const std::vector<MetadataCategory>& criteria);
    static std::vector<ClipId> deterministicClipOrder(const ProjectModel& m, std::vector<ClipId> clips);
};

} // namespace vo
