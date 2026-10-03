#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "app/application/ProjectSession.h"

namespace vo {

struct ImportSummary {
    size_t scanned = 0;
    size_t added = 0;
    size_t alreadyInProject = 0;
    size_t skippedUnsupported = 0;
    std::vector<std::string> errors;
    bool cancelled = false;
};

// Recursive import of a directory inside the project root (spec §15, §57):
// Scanning -> Hashing -> Metadata extraction -> Database insertion, with
// progress events and batched transactions. Existing subdirectories become
// IMPORTED folders; files on disk are never moved or renamed by import.
class ImportService {
public:
    // `directory` may be absolute (must be inside the root) or relative.
    static ImportSummary run(ProjectSession& session, const std::string& directory,
                             const std::atomic<bool>* cancel = nullptr);
};

} // namespace vo
