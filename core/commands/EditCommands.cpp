#include "core/commands/EditCommands.h"

#include <algorithm>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo::commands {

namespace {

const Folder& requireFolder(const ProjectModel& m, const FolderId& id) {
    const Folder* f = m.folder(id);
    if (!f) throw UserError("Folder not found");
    return *f;
}

const Clip& requireClip(const ProjectModel& m, const ClipId& id) {
    const Clip* c = m.clip(id);
    if (!c) throw UserError("Clip not found");
    return *c;
}

std::string validatedName(const std::string& raw, const char* what) {
    std::string name = trim(raw);
    std::string reason;
    if (!path::isValidName(name, &reason)) throw UserError(std::string("Invalid ") + what + " name: " + reason);
    return name;
}

bool folderNameTaken(const ProjectModel& m, const FolderId& parent, const std::string& name, const FolderId& exclude) {
    auto existing = m.childFolderNamed(parent, name);
    if (existing && *existing != exclude) return true;
    for (const auto& cid : m.clipsInFolder(parent))
        if (equalsIgnoreCase(m.clip(cid)->intendedFilename(), name)) return true;
    return false;
}

// Moving a folder by hand is a deliberate placement of everything inside it.
void markSubtreeManual(ProjectModel& m, const FolderId& folderId) {
    for (const auto& cid : m.subtreeClips(folderId)) {
        Clip c = *m.clip(cid);
        if (!c.placementOverride) {
            c.placementOverride = true;
            c.updatedAt = nowUnixSeconds();
            m.putClip(c);
        }
    }
}

std::vector<ClipId> sortedClips(const ProjectModel& m, std::vector<ClipId> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::sort(ids.begin(), ids.end(), [&](const ClipId& a, const ClipId& b) {
        auto pa = m.clipIntendedPath(a), pb = m.clipIntendedPath(b);
        if (pa != pb) return naturalLess(pa, pb);
        return a < b;
    });
    return ids;
}

} // namespace

bool clipTitleTaken(const ProjectModel& m, const FolderId& folder, const std::string& title,
                    const std::string& extension, const ClipId& exclude) {
    for (const auto& cid : m.clipsInFolder(folder)) {
        if (cid == exclude) continue;
        if (equalsIgnoreCase(m.clip(cid)->title, title)) return true;
    }
    if (m.childFolderNamed(folder, title + extension)) return true;
    return false;
}

std::string uniqueClipTitle(const ProjectModel& m, const FolderId& folder, const std::string& desired,
                            const std::string& extension, const ClipId& exclude) {
    if (!clipTitleTaken(m, folder, desired, extension, exclude)) return desired;
    for (int n = 2;; ++n) {
        std::string candidate = desired + " (" + std::to_string(n) + ")";
        if (!clipTitleTaken(m, folder, candidate, extension, exclude)) return candidate;
    }
}

std::string uniqueFolderName(const ProjectModel& m, const FolderId& parent, const std::string& desired,
                             const FolderId& exclude) {
    if (!folderNameTaken(m, parent, desired, exclude)) return desired;
    for (int n = 2;; ++n) {
        std::string candidate = desired + " " + std::to_string(n);
        if (!folderNameTaken(m, parent, candidate, exclude)) return candidate;
    }
}

FolderId createFolder(ProjectModel& m, const FolderId& parent, const std::string& name) {
    requireFolder(m, parent);
    std::string clean = validatedName(name.empty() ? "untitled folder" : name, "folder");
    Folder f;
    f.id = generateId();
    f.parentId = parent;
    f.name = uniqueFolderName(m, parent, clean, "");
    f.orderIndex = static_cast<int>(m.childFolderCount(parent));
    f.origin = FolderOrigin::Manual;
    m.putFolder(f);
    return f.id;
}

void renameFolder(ProjectModel& m, const FolderId& id, const std::string& name) {
    Folder f = requireFolder(m, id);
    if (f.parentId.empty()) throw UserError("The project root folder cannot be renamed");
    std::string clean = validatedName(name, "folder");
    if (clean == f.name) return;
    if (folderNameTaken(m, f.parentId, clean, id))
        throw UserError("An item named \"" + clean + "\" already exists in this folder");
    f.name = clean;
    f.origin = FolderOrigin::Manual;
    m.putFolder(f);
    markSubtreeManual(m, id);
}

void moveFolders(ProjectModel& m, const std::vector<FolderId>& ids, const FolderId& destination) {
    requireFolder(m, destination);
    for (const auto& id : ids) {
        Folder f = requireFolder(m, id);
        if (f.parentId.empty()) throw UserError("The project root folder cannot be moved");
        if (m.isAncestorOrSelf(id, destination))
            throw UserError("Cannot move folder \"" + f.name + "\" into itself or one of its subfolders");
    }
    for (const auto& id : ids) {
        Folder f = *m.folder(id);
        if (f.parentId == destination) continue;
        f.name = uniqueFolderName(m, destination, f.name, id);
        f.parentId = destination;
        f.orderIndex = static_cast<int>(m.childFolderCount(destination));
        f.origin = FolderOrigin::Manual;
        m.putFolder(f);
        markSubtreeManual(m, id);
    }
}

void deleteFolder(ProjectModel& m, const FolderId& id, DeleteFolderBehavior behavior) {
    const Folder& f = requireFolder(m, id);
    if (f.parentId.empty()) throw UserError("The project root folder cannot be deleted");
    FolderId parent = f.parentId;
    auto subtree = m.subtreeFolders(id);
    auto clips = m.subtreeClips(id);
    if (behavior == DeleteFolderBehavior::MoveClipsToParent) {
        for (const auto& cid : clips) {
            Clip c = *m.clip(cid);
            c.title = uniqueClipTitle(m, parent, c.title, c.extension, cid);
            c.folderId = parent;
            c.placementOverride = true;
            c.updatedAt = nowUnixSeconds();
            m.putClip(c);
        }
    } else {
        removeClips(m, clips);
    }
    for (auto it = subtree.rbegin(); it != subtree.rend(); ++it) m.eraseFolder(*it);
}

void moveClips(ProjectModel& m, const std::vector<ClipId>& ids, const FolderId& destination) {
    requireFolder(m, destination);
    for (const auto& id : ids) requireClip(m, id);
    for (const auto& id : sortedClips(m, ids)) {
        Clip c = *m.clip(id);
        if (c.folderId == destination) continue;
        c.title = uniqueClipTitle(m, destination, c.title, c.extension, id);
        c.folderId = destination;
        c.placementOverride = true;
        c.updatedAt = nowUnixSeconds();
        m.putClip(c);
    }
}

void renameClip(ProjectModel& m, const ClipId& id, const std::string& rawTitle) {
    Clip c = requireClip(m, id);
    std::string title = trim(rawTitle);
    // Users often type the extension; the extension itself is preserved (FR-NAME-004).
    if (!c.extension.empty() && endsWithIgnoreCase(title, c.extension) && title.size() > c.extension.size())
        title = trim(title.substr(0, title.size() - c.extension.size()));
    std::string reason;
    if (!path::isValidName(title + c.extension, &reason) || title.empty())
        throw UserError("Invalid clip name: " + (title.empty() ? std::string("Name is empty") : reason));
    if (title == c.title && c.titleOverride) return;
    c.title = uniqueClipTitle(m, c.folderId, title, c.extension, id);
    c.titleOverride = true;
    c.updatedAt = nowUnixSeconds();
    m.putClip(c);
}

void removeClips(ProjectModel& m, const std::vector<ClipId>& ids) {
    for (const auto& id : ids) {
        if (!m.clip(id)) continue;
        m.setClipMetadata(id, nullptr);
        m.setClipTags(id, {});
        m.eraseClip(id);
    }
}

TagId createTag(ProjectModel& m, const std::string& name) {
    std::string clean = trim(name);
    if (clean.empty()) throw UserError("Tag name is empty");
    if (const Tag* existing = m.tagByName(clean)) return existing->id;
    Tag t{generateId(), clean};
    m.putTag(t);
    return t.id;
}

void renameTag(ProjectModel& m, const TagId& id, const std::string& name) {
    const Tag* t = m.tag(id);
    if (!t) throw UserError("Tag not found");
    std::string clean = trim(name);
    if (clean.empty()) throw UserError("Tag name is empty");
    if (const Tag* other = m.tagByName(clean); other && other->id != id)
        throw UserError("A tag named \"" + clean + "\" already exists");
    m.putTag(Tag{id, clean});
}

void deleteTag(ProjectModel& m, const TagId& id) {
    if (!m.tag(id)) throw UserError("Tag not found");
    std::vector<ClipId> tagged;
    for (const auto& [cid, c] : m.clips())
        if (m.clipTags(cid).count(id)) tagged.push_back(cid);
    std::sort(tagged.begin(), tagged.end());
    for (const auto& cid : tagged) {
        auto tags = m.clipTags(cid);
        tags.erase(id);
        m.setClipTags(cid, tags);
    }
    m.eraseTag(id);
}

void addTag(ProjectModel& m, const std::vector<ClipId>& ids, const TagId& tag) {
    if (!m.tag(tag)) throw UserError("Tag not found");
    for (const auto& id : ids) {
        requireClip(m, id);
        auto tags = m.clipTags(id);
        if (tags.insert(tag).second) m.setClipTags(id, tags);
    }
}

void removeTag(ProjectModel& m, const std::vector<ClipId>& ids, const TagId& tag) {
    for (const auto& id : ids) {
        requireClip(m, id);
        auto tags = m.clipTags(id);
        if (tags.erase(tag)) m.setClipTags(id, tags);
    }
}

} // namespace vo::commands
