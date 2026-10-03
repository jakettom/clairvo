#include "core/filesystem/PreflightValidator.h"

#include <map>
#include <set>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

std::string toString(IssueSeverity s) {
    switch (s) {
    case IssueSeverity::Error: return "error";
    case IssueSeverity::Warning: return "warning";
    case IssueSeverity::Info: return "info";
    }
    return "error";
}

size_t PreflightReport::errorCount() const {
    size_t n = 0;
    for (const auto& i : issues) n += i.severity == IssueSeverity::Error;
    return n;
}

size_t PreflightReport::warningCount() const {
    size_t n = 0;
    for (const auto& i : issues) n += i.severity == IssueSeverity::Warning;
    return n;
}

PreflightReport runPreflight(const ProjectModel& m, const ChangeSet& changes, FileSystem& fs) {
    PreflightReport r;
    const std::string& root = m.info.rootPath;
    const std::string projectFileName = path::filename(m.info.projectFile);
    auto abs = [&](const std::string& rel) { return path::absolute(root, rel); };

    // Translate a post-Apply path into where that location is on disk *now*,
    // undoing scheduled directory relocations (latest first).
    std::vector<std::pair<std::string, std::string>> relocations;
    for (const auto& c : changes.folderChanges)
        if (c.type != ChangeType::CreateFolder) relocations.emplace_back(c.sourcePath, c.destinationPath);
    auto currentDiskPath = [&](std::string p) {
        for (auto it = relocations.rbegin(); it != relocations.rend(); ++it) p = path::rebase(p, it->second, it->first);
        return p;
    };

    auto issue = [&](IssueSeverity sev, std::string msg, std::string p, ClipId clip = {}, FolderId folder = {}) {
        if (sev == IssueSeverity::Error) r.ok = false;
        r.issues.push_back({sev, std::move(msg), std::move(p), std::move(clip), std::move(folder)});
    };

    size_t errorsBefore = 0;
    auto check = [&](const std::string& description) {
        r.checks.push_back({description, r.errorCount() == errorsBefore});
        errorsBefore = r.errorCount();
    };

    // --- Root -----------------------------------------------------------------
    FileInfo rootInfo = fs.info(root);
    if (!rootInfo.isDirectory) issue(IssueSeverity::Error, "Project root folder is not accessible", root);
    else if (!fs.isWritable(root)) issue(IssueSeverity::Error, "Project root folder is not writable", root);
    check("Project root is accessible");

    // --- Project file protection ----------------------------------------------
    for (const auto& c : changes.clipChanges) {
        if (equalsIgnoreCase(c.destinationPath, projectFileName) || equalsIgnoreCase(c.sourcePath, projectFileName))
            issue(IssueSeverity::Error, "Change would touch the project file", c.destinationPath, c.clipId);
    }
    for (const auto& c : changes.folderChanges) {
        if (equalsIgnoreCase(c.destinationPath, projectFileName))
            issue(IssueSeverity::Error, "Folder would replace the project file", c.destinationPath, {}, c.folderId);
    }
    check("Project file is protected");

    // --- Folder operations ----------------------------------------------------
    for (const auto& c : changes.folderChanges) {
        std::string reason;
        if (!path::isValidName(c.newName, &reason))
            issue(IssueSeverity::Error, "Invalid folder name \"" + c.newName + "\": " + reason, c.destinationPath, {}, c.folderId);
        if (c.type == ChangeType::CreateFolder) {
            ++r.folderCreates;
            FileInfo fi = fs.info(abs(currentDiskPath(c.destinationPath)));
            if (fi.exists && !fi.isDirectory)
                issue(IssueSeverity::Error, "A file already exists where a folder would be created", c.destinationPath, {}, c.folderId);
            else if (fi.isDirectory)
                issue(IssueSeverity::Info, "Existing directory will be used", c.destinationPath, {}, c.folderId);
            continue;
        }
        ++r.folderMoves;
        const Folder* f = m.folder(c.folderId);
        if (!f || !f->physicalPath || !fs.isDirectory(abs(*f->physicalPath))) {
            issue(IssueSeverity::Error, "Folder to rename/move no longer exists on disk", c.sourcePath, {}, c.folderId);
            continue;
        }
        if (path::isWithin(c.destinationPath, c.sourcePath))
            issue(IssueSeverity::Error, "Folder cannot be moved into itself", c.destinationPath, {}, c.folderId);
        std::string destNow = currentDiskPath(c.destinationPath);
        bool caseOnly = equalsIgnoreCase(destNow, *f->physicalPath);
        if (!caseOnly && fs.exists(abs(destNow)))
            issue(IssueSeverity::Error, "Destination folder already exists on disk", c.destinationPath, {}, c.folderId);
        std::string parentNow = path::parent(*f->physicalPath);
        if (fs.isDirectory(abs(parentNow)) && !fs.isWritable(abs(parentNow)))
            issue(IssueSeverity::Error, "No permission to modify folder", parentNow, {}, c.folderId);
    }
    check("Folder operations are valid");

    // --- Clip operations --------------------------------------------------------
    std::set<std::string> currentClipPaths; // lowercased current paths of all project clips
    for (const auto& [id, c] : m.clips()) currentClipPaths.insert(toLower(c.filePath));

    std::map<std::string, ClipId> finalPaths; // lowercased final path -> clip
    std::set<ClipId> moving;
    for (const auto& c : changes.clipChanges) moving.insert(c.clipId);
    for (const auto& [id, c] : m.clips()) {
        std::string finalPath = moving.count(id) ? m.clipIntendedPath(id) : c.filePath;
        if (c.status == ClipStatus::Missing && !moving.count(id)) continue;
        auto [it, inserted] = finalPaths.emplace(toLower(finalPath), id);
        if (!inserted) {
            issue(IssueSeverity::Error, "Two clips would end up with the same file name", finalPath, id);
        }
    }

    std::set<std::string> checkedWritable;
    auto requireWritable = [&](const std::string& relDir, const ClipId& clip) {
        if (!checkedWritable.insert(relDir).second) return;
        std::string a = abs(relDir);
        if (fs.isDirectory(a) && !fs.isWritable(a)) issue(IssueSeverity::Error, "No permission to modify folder", relDir, clip);
    };

    for (const auto& c : changes.clipChanges) {
        if (c.moves) ++r.clipMoves;
        if (c.renames) ++r.clipRenames;
        const Clip* clip = m.clip(c.clipId);
        if (!clip) continue;
        if (!fs.info(abs(clip->filePath)).isRegularFile) {
            issue(IssueSeverity::Error, "Source file not found", clip->filePath, c.clipId);
            continue;
        }
        std::string reason;
        std::string name = path::filename(c.destinationPath);
        if (!path::isValidName(name, &reason))
            issue(IssueSeverity::Error, "Invalid file name \"" + name + "\": " + reason, c.destinationPath, c.clipId);
        std::string destNow = currentDiskPath(c.destinationPath);
        bool caseOnly = equalsIgnoreCase(destNow, clip->filePath);
        if (!caseOnly && fs.exists(abs(destNow)) && !currentClipPaths.count(toLower(destNow)))
            issue(IssueSeverity::Error, "Destination is occupied by a file that is not part of this project",
                  c.destinationPath, c.clipId);
        requireWritable(path::parent(clip->filePath), c.clipId);
        requireWritable(path::parent(destNow), c.clipId);
    }
    check("Source files exist and destinations are free");

    // --- Missing clips ------------------------------------------------------------
    if (!changes.skippedMissing.empty()) {
        for (const auto& id : changes.skippedMissing)
            issue(IssueSeverity::Warning, "Missing clip will be skipped", m.clip(id)->filePath, id);
    }
    r.checks.push_back({"All referenced files are available", changes.skippedMissing.empty()});

    return r;
}

} // namespace vo
