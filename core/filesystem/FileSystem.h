#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vo {

enum class FsErrorKind { NotFound, AlreadyExists, PermissionDenied, NotADirectory, NotEmpty, CrossDevice, InvalidName, Other };

struct FsError {
    FsErrorKind kind = FsErrorKind::Other;
    int code = 0;
    std::string message; // user-facing reason, e.g. "Permission denied"
};

struct FileInfo {
    bool exists = false;
    bool isDirectory = false;
    bool isRegularFile = false;
    uint64_t size = 0;
    int64_t modifiedAt = 0;
};

struct DirEntry {
    std::string name;
    bool isDirectory = false;
    bool isRegularFile = false;
    bool isSymlink = false;
};

// All physical filesystem access goes through this interface (spec §49) so
// the core can be tested with temporary directories and fault injection.
// Paths are absolute. No operation ever overwrites or deletes a file.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual FileInfo info(const std::string& path) = 0;
    bool exists(const std::string& path) { return info(path).exists; }
    bool isDirectory(const std::string& path) { return info(path).isDirectory; }

    // Creates one directory level. Fails with AlreadyExists if anything exists.
    virtual std::optional<FsError> createDirectory(const std::string& path) = 0;
    // Atomically moves/renames a file or directory; fails with AlreadyExists
    // instead of replacing an existing destination.
    virtual std::optional<FsError> moveNoReplace(const std::string& source, const std::string& destination) = 0;
    std::optional<FsError> renameFile(const std::string& path, const std::string& newName);
    // Removes a directory only if it is empty (or contains nothing but a
    // Finder .DS_Store). Never removes files with content.
    virtual std::optional<FsError> removeEmptyDirectory(const std::string& path) = 0;
    virtual std::vector<DirEntry> listDirectory(const std::string& path, std::optional<FsError>* error = nullptr) = 0;
    virtual bool isWritable(const std::string& path) = 0;
};

class PosixFileSystem : public FileSystem {
public:
    FileInfo info(const std::string& path) override;
    std::optional<FsError> createDirectory(const std::string& path) override;
    std::optional<FsError> moveNoReplace(const std::string& source, const std::string& destination) override;
    std::optional<FsError> removeEmptyDirectory(const std::string& path) override;
    std::vector<DirEntry> listDirectory(const std::string& path, std::optional<FsError>* error) override;
    bool isWritable(const std::string& path) override;

    static FsError errorFromErrno(int err);
};

} // namespace vo
