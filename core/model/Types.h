#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/metadata/MetadataCategory.h"
#include "core/model/Ids.h"

namespace vo {

enum class ClipStatus { Available, Missing };

// Where a folder came from. Automatic organization may prune empty
// IMPORTED/AUTOMATIC folders but never MANUAL ones.
enum class FolderOrigin { Imported, Manual, Automatic };

std::string toString(ClipStatus s);
std::string toString(FolderOrigin o);
ClipStatus clipStatusFromString(const std::string& s);
FolderOrigin folderOriginFromString(const std::string& s);

struct ProjectInfo {
    ProjectId id;
    std::string name;
    std::string rootPath;    // absolute path of the footage root (never renamed)
    std::string projectFile; // absolute path of <Name>.project inside the root
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
};

struct Folder {
    FolderId id;
    FolderId parentId; // empty only for the root folder
    std::string name;
    int orderIndex = 0;
    FolderOrigin origin = FolderOrigin::Manual;
    // Confirmed directory on disk, relative to the root. Empty optional means
    // the folder is virtual-only and Apply will create it. The root is "".
    std::optional<std::string> physicalPath;
    bool operator==(const Folder&) const = default;
};

struct Clip {
    ClipId id;
    FolderId folderId;          // intended (virtual) location — exactly one folder
    std::string filePath;       // confirmed physical path relative to root
    std::string originalFilename;
    std::string title;          // intended filename without extension
    std::string extension;      // preserved extension incl. dot, e.g. ".MOV"
    std::string fileHash;       // fingerprint only; never the identity
    int64_t fileSize = 0;
    ClipStatus status = ClipStatus::Available;
    bool placementOverride = false; // user manually placed the clip
    bool titleOverride = false;     // user manually titled the clip
    int64_t createdAt = 0;
    int64_t updatedAt = 0;
    bool operator==(const Clip&) const = default;

    std::string intendedFilename() const { return title + extension; }
};

struct Tag {
    TagId id;
    std::string name;
    bool operator==(const Tag&) const = default;
};

struct OrganizationConfig {
    std::vector<MetadataCategory> criteria; // ordered: first = top level
    std::string namingTemplate = "{folder}_{number}";
    bool operator==(const OrganizationConfig&) const = default;
};

// Thrown for user-facing validation failures (invalid names, illegal moves…).
// Edits that throw are rolled back before the error propagates.
struct UserError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace vo
