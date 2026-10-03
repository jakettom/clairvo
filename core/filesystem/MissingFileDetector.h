#pragma once

#include <vector>

#include "core/filesystem/FileSystem.h"
#include "core/model/ProjectModel.h"

namespace vo {

struct RefreshResult {
    std::vector<ClipId> newlyMissing;
    std::vector<ClipId> recovered;     // previously missing, now found at the expected path
    std::vector<FolderId> detachedFolders; // physical directory no longer exists
};

// Re-validates the confirmed physical state (spec §30): clips whose file is
// gone become MISSING (never removed or relocated), and folders whose
// directory vanished become virtual-only so Apply would recreate them.
RefreshResult refreshPhysicalState(ProjectModel& m, FileSystem& fs);

} // namespace vo
