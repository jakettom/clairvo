#include "core/changes/ChangeSet.h"

#include <algorithm>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

std::string toString(ChangeType t) {
    switch (t) {
    case ChangeType::CreateFolder: return "CREATE_FOLDER";
    case ChangeType::RenameFolder: return "RENAME_FOLDER";
    case ChangeType::MoveFolder: return "MOVE_FOLDER";
    case ChangeType::MoveClip: return "MOVE_CLIP";
    case ChangeType::RenameClip: return "RENAME_CLIP";
    }
    return "UNKNOWN";
}

std::string toString(ChangeOrigin o) { return o == ChangeOrigin::Manual ? "MANUAL" : "AUTOMATIC"; }

std::vector<FolderId> foldersTopDown(const ProjectModel& m) {
    if (m.rootFolderId().empty()) return {};
    return m.subtreeFolders(m.rootFolderId());
}

namespace {

// Sequence of directory relocations already scheduled earlier in the plan.
// A later path is expressed in post-relocation coordinates by applying the
// mappings in order.
struct PrefixMap {
    std::vector<std::pair<std::string, std::string>> mappings;
    std::string apply(std::string p) const {
        for (const auto& [from, to] : mappings) p = path::rebase(p, from, to);
        return p;
    }
};

} // namespace

ChangeSet computeChanges(const ProjectModel& m) {
    ChangeSet out;
    PrefixMap moved;

    for (const auto& id : foldersTopDown(m)) {
        const Folder& f = *m.folder(id);
        if (f.parentId.empty()) continue;
        std::string intended = m.folderPath(id);
        ChangeOrigin origin = f.origin == FolderOrigin::Automatic ? ChangeOrigin::Automatic : ChangeOrigin::Manual;
        if (!f.physicalPath) {
            Change c;
            c.type = ChangeType::CreateFolder;
            c.origin = origin;
            c.folderId = id;
            c.destinationPath = intended;
            c.newName = f.name;
            out.folderChanges.push_back(std::move(c));
            continue;
        }
        std::string effective = moved.apply(*f.physicalPath);
        if (effective == intended) continue;
        Change c;
        c.folderId = id;
        c.origin = origin;
        c.sourcePath = effective;
        c.destinationPath = intended;
        c.oldName = path::filename(effective);
        c.newName = f.name;
        c.moves = path::parent(effective) != path::parent(intended);
        c.renames = c.oldName != c.newName;
        c.type = c.moves ? ChangeType::MoveFolder : ChangeType::RenameFolder;
        moved.mappings.emplace_back(effective, intended);
        out.folderChanges.push_back(std::move(c));
    }

    std::vector<std::pair<std::string, ClipId>> clips;
    clips.reserve(m.clips().size());
    for (const auto& [id, c] : m.clips()) clips.emplace_back(m.clipIntendedPath(id), id);
    std::sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return naturalLess(a.first, b.first);
        return a.second < b.second;
    });

    for (const auto& [intended, id] : clips) {
        const Clip& clip = *m.clip(id);
        std::string effective = moved.apply(clip.filePath);
        if (effective == intended) continue;
        if (clip.status == ClipStatus::Missing) {
            out.skippedMissing.push_back(id);
            continue;
        }
        Change c;
        c.clipId = id;
        c.origin = (clip.placementOverride || clip.titleOverride) ? ChangeOrigin::Manual : ChangeOrigin::Automatic;
        c.sourcePath = effective;
        c.destinationPath = intended;
        c.oldName = path::filename(effective);
        c.newName = path::filename(intended);
        c.moves = path::parent(effective) != path::parent(intended);
        c.renames = c.oldName != c.newName;
        c.type = c.moves ? ChangeType::MoveClip : ChangeType::RenameClip;
        out.clipChanges.push_back(std::move(c));
    }
    return out;
}

} // namespace vo
