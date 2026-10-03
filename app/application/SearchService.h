#pragma once

#include <string>
#include <vector>

#include "core/model/ProjectModel.h"

namespace vo {

struct SearchResult {
    std::vector<ClipId> clips;
    std::vector<FolderId> folders;
};

// Simple local search (spec §38): every whitespace-separated term must match
// (case-insensitively) one of title, original filename, folder path, tag
// names, or normalized metadata values. No video-content analysis.
SearchResult search(const ProjectModel& m, const std::string& query);

} // namespace vo
