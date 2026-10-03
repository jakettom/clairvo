#include <cerrno>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/filesystem/FileSystem.h"
#include "core/util/PathUtil.h"

namespace vo {

std::optional<FsError> FileSystem::renameFile(const std::string& p, const std::string& newName) {
    std::string reason;
    if (!path::isValidName(newName, &reason)) return FsError{FsErrorKind::InvalidName, EINVAL, reason};
    auto slash = p.rfind('/');
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash);
    return moveNoReplace(p, dir.empty() ? newName : dir + "/" + newName);
}

FsError PosixFileSystem::errorFromErrno(int err) {
    FsError e;
    e.code = err;
    switch (err) {
    case ENOENT: e.kind = FsErrorKind::NotFound; e.message = "No such file or directory"; break;
    case EEXIST: e.kind = FsErrorKind::AlreadyExists; e.message = "Destination already exists"; break;
    case EACCES:
    case EPERM:
    case EROFS: e.kind = FsErrorKind::PermissionDenied; e.message = "Permission denied"; break;
    case ENOTDIR: e.kind = FsErrorKind::NotADirectory; e.message = "A path component is not a directory"; break;
    case ENOTEMPTY: e.kind = FsErrorKind::NotEmpty; e.message = "Directory is not empty"; break;
    case EXDEV: e.kind = FsErrorKind::CrossDevice; e.message = "Cannot move across volumes"; break;
    case ENAMETOOLONG: e.kind = FsErrorKind::InvalidName; e.message = "Name is too long"; break;
    default: e.kind = FsErrorKind::Other; e.message = std::strerror(err); break;
    }
    return e;
}

FileInfo PosixFileSystem::info(const std::string& p) {
    FileInfo fi;
    struct stat st{};
    if (::lstat(p.c_str(), &st) != 0) return fi;
    fi.exists = true;
    fi.isDirectory = S_ISDIR(st.st_mode);
    fi.isRegularFile = S_ISREG(st.st_mode);
    fi.size = static_cast<uint64_t>(st.st_size);
    fi.modifiedAt = static_cast<int64_t>(st.st_mtimespec.tv_sec);
    return fi;
}

std::optional<FsError> PosixFileSystem::createDirectory(const std::string& p) {
    if (::mkdir(p.c_str(), 0755) != 0) return errorFromErrno(errno);
    return std::nullopt;
}

std::optional<FsError> PosixFileSystem::moveNoReplace(const std::string& source, const std::string& destination) {
    if (source == destination) return FsError{FsErrorKind::AlreadyExists, EEXIST, "Source and destination are identical"};
    struct stat st{};
    if (::lstat(source.c_str(), &st) != 0) {
        FsError e = errorFromErrno(errno);
        if (e.kind == FsErrorKind::NotFound) e.message = "Source not found";
        return e;
    }
    // RENAME_EXCL makes the no-overwrite guarantee atomic (no check-then-act race).
    if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) != 0) {
        int err = errno;
        FsError e = errorFromErrno(err);
        if (e.kind == FsErrorKind::NotFound) e.message = "Destination folder does not exist";
        return e;
    }
    return std::nullopt;
}

std::optional<FsError> PosixFileSystem::removeEmptyDirectory(const std::string& p) {
    std::optional<FsError> listErr;
    auto entries = listDirectory(p, &listErr);
    if (listErr) return listErr;
    for (const auto& e : entries) {
        if (e.name != ".DS_Store") return FsError{FsErrorKind::NotEmpty, ENOTEMPTY, "Directory is not empty"};
    }
    for (const auto& e : entries) {
        // Finder view metadata only; never media.
        if (e.name == ".DS_Store" && e.isRegularFile) ::unlink((p + "/.DS_Store").c_str());
    }
    if (::rmdir(p.c_str()) != 0) return errorFromErrno(errno);
    return std::nullopt;
}

std::vector<DirEntry> PosixFileSystem::listDirectory(const std::string& p, std::optional<FsError>* error) {
    std::vector<DirEntry> out;
    DIR* dir = ::opendir(p.c_str());
    if (!dir) {
        if (error) *error = errorFromErrno(errno);
        return out;
    }
    while (struct dirent* ent = ::readdir(dir)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        DirEntry e;
        e.name = name;
        if (ent->d_type == DT_UNKNOWN) {
            FileInfo fi = info(p + "/" + name);
            e.isDirectory = fi.isDirectory;
            e.isRegularFile = fi.isRegularFile;
        } else {
            e.isDirectory = ent->d_type == DT_DIR;
            e.isRegularFile = ent->d_type == DT_REG;
            e.isSymlink = ent->d_type == DT_LNK;
        }
        out.push_back(std::move(e));
    }
    ::closedir(dir);
    return out;
}

bool PosixFileSystem::isWritable(const std::string& p) { return ::access(p.c_str(), W_OK) == 0; }

} // namespace vo
