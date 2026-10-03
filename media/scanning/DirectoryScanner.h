#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vo::media {

struct ScannedFile {
    std::string relativePath; // relative to the project root
    uint64_t size = 0;
    int64_t modifiedAt = 0;
};

struct ScanResult {
    std::vector<ScannedFile> files;
    std::vector<std::string> unreadableDirectories;
    size_t skippedUnsupported = 0;
};

// Recursively finds supported video files below root/startRel (spec §15).
// Hidden entries, symlinks, package directories and *.project files are
// skipped. Results are sorted for determinism.
ScanResult scanDirectory(const std::string& root, const std::string& startRel,
                         const std::function<void(size_t found)>& progress = {},
                         const std::atomic<bool>* cancel = nullptr);

} // namespace vo::media
