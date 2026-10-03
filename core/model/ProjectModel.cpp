#include "core/model/ProjectModel.h"

#include <algorithm>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

std::string toString(ClipStatus s) { return s == ClipStatus::Available ? "AVAILABLE" : "MISSING"; }

std::string toString(FolderOrigin o) {
    switch (o) {
    case FolderOrigin::Imported: return "IMPORTED";
    case FolderOrigin::Automatic: return "AUTOMATIC";
    case FolderOrigin::Manual: break;
    }
    return "MANUAL";
}

ClipStatus clipStatusFromString(const std::string& s) {
    return s == "MISSING" ? ClipStatus::Missing : ClipStatus::Available;
}

FolderOrigin folderOriginFromString(const std::string& s) {
    if (s == "IMPORTED") return FolderOrigin::Imported;
    if (s == "AUTOMATIC") return FolderOrigin::Automatic;
    return FolderOrigin::Manual;
}

// ---- Reads -----------------------------------------------------------------

const Folder* ProjectModel::folder(const FolderId& id) const {
    auto it = folders_.find(id);
    return it == folders_.end() ? nullptr : &it->second;
}

const Clip* ProjectModel::clip(const ClipId& id) const {
    auto it = clips_.find(id);
    return it == clips_.end() ? nullptr : &it->second;
}

const Tag* ProjectModel::tag(const TagId& id) const {
    auto it = tags_.find(id);
    return it == tags_.end() ? nullptr : &it->second;
}

const Tag* ProjectModel::tagByName(const std::string& name) const {
    for (const auto& [id, t] : tags_)
        if (equalsIgnoreCase(t.name, name)) return &t;
    return nullptr;
}

std::vector<FolderId> ProjectModel::childFolders(const FolderId& id) const {
    std::vector<FolderId> out;
    auto it = childFolders_.find(id);
    if (it == childFolders_.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end(), [&](const FolderId& a, const FolderId& b) {
        const auto& fa = folders_.at(a);
        const auto& fb = folders_.at(b);
        if (fa.name != fb.name) return naturalLess(fa.name, fb.name);
        return a < b;
    });
    return out;
}

std::vector<ClipId> ProjectModel::clipsInFolder(const FolderId& id) const {
    std::vector<ClipId> out;
    auto it = folderClips_.find(id);
    if (it == folderClips_.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end(), [&](const ClipId& a, const ClipId& b) {
        const auto& ca = clips_.at(a);
        const auto& cb = clips_.at(b);
        auto na = ca.intendedFilename(), nb = cb.intendedFilename();
        if (na != nb) return naturalLess(na, nb);
        return a < b;
    });
    return out;
}

size_t ProjectModel::childFolderCount(const FolderId& id) const {
    auto it = childFolders_.find(id);
    return it == childFolders_.end() ? 0 : it->second.size();
}

size_t ProjectModel::clipCount(const FolderId& id) const {
    auto it = folderClips_.find(id);
    return it == folderClips_.end() ? 0 : it->second.size();
}

const std::set<TagId>& ProjectModel::clipTags(const ClipId& id) const {
    static const std::set<TagId> empty;
    auto it = clipTags_.find(id);
    return it == clipTags_.end() ? empty : it->second;
}

std::shared_ptr<const ClipMetadata> ProjectModel::metadata(const ClipId& id) const {
    auto it = metadata_.find(id);
    return it == metadata_.end() ? nullptr : it->second;
}

std::optional<std::string> ProjectModel::normalizedValue(const ClipId& id, MetadataCategory c) const {
    auto md = metadata(id);
    if (!md) return std::nullopt;
    auto it = md->normalized.find(c);
    if (it == md->normalized.end() || it->second.empty()) return std::nullopt;
    return it->second;
}

std::vector<std::string> ProjectModel::folderPathComponents(const FolderId& id) const {
    std::vector<std::string> parts;
    const Folder* f = folder(id);
    size_t guard = 0;
    while (f && !f->parentId.empty() && guard++ < folders_.size() + 1) {
        parts.push_back(f->name);
        f = folder(f->parentId);
    }
    std::reverse(parts.begin(), parts.end());
    return parts;
}

std::string ProjectModel::folderPath(const FolderId& id) const { return join(folderPathComponents(id), "/"); }

std::string ProjectModel::clipIntendedPath(const ClipId& id) const {
    const Clip* c = clip(id);
    if (!c) return "";
    return path::join(folderPath(c->folderId), c->intendedFilename());
}

bool ProjectModel::isAncestorOrSelf(const FolderId& ancestor, const FolderId& id) const {
    const Folder* f = folder(id);
    size_t guard = 0;
    while (f && guard++ < folders_.size() + 1) {
        if (f->id == ancestor) return true;
        if (f->parentId.empty()) return false;
        f = folder(f->parentId);
    }
    return false;
}

std::vector<FolderId> ProjectModel::subtreeFolders(const FolderId& id) const {
    std::vector<FolderId> out;
    if (!folder(id)) return out;
    std::vector<FolderId> stack{id};
    while (!stack.empty()) {
        FolderId cur = stack.back();
        stack.pop_back();
        out.push_back(cur);
        auto kids = childFolders(cur);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
    return out;
}

std::vector<ClipId> ProjectModel::subtreeClips(const FolderId& id) const {
    std::vector<ClipId> out;
    for (const auto& f : subtreeFolders(id)) {
        auto clips = clipsInFolder(f);
        out.insert(out.end(), clips.begin(), clips.end());
    }
    return out;
}

std::optional<FolderId> ProjectModel::childFolderNamed(const FolderId& parent, const std::string& name) const {
    auto it = childFolders_.find(parent);
    if (it == childFolders_.end()) return std::nullopt;
    // Deterministic choice if (unexpectedly) several match.
    std::optional<FolderId> best;
    for (const auto& id : it->second) {
        if (equalsIgnoreCase(folders_.at(id).name, name) && (!best || id < *best)) best = id;
    }
    return best;
}

std::optional<FolderId> ProjectModel::folderWithPhysicalPath(const std::string& relPath) const {
    std::optional<FolderId> best;
    for (const auto& [id, f] : folders_) {
        if (f.physicalPath && equalsIgnoreCase(*f.physicalPath, relPath) && (!best || id < *best)) best = id;
    }
    return best;
}

std::optional<ClipId> ProjectModel::clipWithFilePath(const std::string& relPath) const {
    auto it = clipByLowerPath_.find(toLower(relPath));
    if (it == clipByLowerPath_.end()) return std::nullopt;
    return it->second;
}

// ---- State setters (shared by mutation, loading and undo) -------------------

void ProjectModel::setFolderState(const FolderId& id, const std::optional<Folder>& f, bool record) {
    std::optional<Folder> before;
    if (auto it = folders_.find(id); it != folders_.end()) {
        before = it->second;
        childFolders_[it->second.parentId].erase(id);
        folders_.erase(it);
    }
    if (f) {
        folders_[id] = *f;
        if (!f->parentId.empty()) childFolders_[f->parentId].insert(id);
        if (f->parentId.empty()) rootFolderId_ = id;
    }
    if (record && before != f) deltas_.push_back(FolderDelta{id, before, f});
}

void ProjectModel::setClipState(const ClipId& id, const std::optional<Clip>& c, bool record) {
    std::optional<Clip> before;
    if (auto it = clips_.find(id); it != clips_.end()) {
        before = it->second;
        folderClips_[it->second.folderId].erase(id);
        auto lp = clipByLowerPath_.find(toLower(it->second.filePath));
        if (lp != clipByLowerPath_.end() && lp->second == id) clipByLowerPath_.erase(lp);
        clips_.erase(it);
    }
    if (c) {
        clips_[id] = *c;
        folderClips_[c->folderId].insert(id);
        clipByLowerPath_[toLower(c->filePath)] = id;
    }
    if (record && before != c) deltas_.push_back(ClipDelta{id, before, c});
}

void ProjectModel::setTagState(const TagId& id, const std::optional<Tag>& t, bool record) {
    std::optional<Tag> before;
    if (auto it = tags_.find(id); it != tags_.end()) {
        before = it->second;
        tags_.erase(it);
    }
    if (t) tags_[id] = *t;
    if (record && before != t) deltas_.push_back(TagDelta{id, before, t});
}

void ProjectModel::setClipTagsState(const ClipId& id, const std::optional<std::set<TagId>>& t, bool record) {
    std::optional<std::set<TagId>> before;
    if (auto it = clipTags_.find(id); it != clipTags_.end()) {
        before = it->second;
        clipTags_.erase(it);
    }
    std::optional<std::set<TagId>> after = t;
    if (after && after->empty()) after.reset();
    if (after) clipTags_[id] = *after;
    if (record && before != after) deltas_.push_back(ClipTagsDelta{id, before, after});
}

void ProjectModel::setClipMetadataState(const ClipId& id,
                                        const std::optional<std::shared_ptr<const ClipMetadata>>& m,
                                        bool record) {
    std::optional<std::shared_ptr<const ClipMetadata>> before;
    if (auto it = metadata_.find(id); it != metadata_.end()) {
        before = it->second;
        metadata_.erase(it);
    }
    std::optional<std::shared_ptr<const ClipMetadata>> after = m;
    if (after && !*after) after.reset();
    if (after) metadata_[id] = *after;
    if (record && before != after) deltas_.push_back(ClipMetadataDelta{id, before, after});
}

// ---- Primitive mutations ------------------------------------------------------

void ProjectModel::putFolder(Folder f) {
    auto id = f.id;
    setFolderState(id, f, true);
}
void ProjectModel::eraseFolder(const FolderId& id) { setFolderState(id, std::nullopt, true); }

void ProjectModel::putClip(Clip c) {
    auto id = c.id;
    setClipState(id, c, true);
}
void ProjectModel::eraseClip(const ClipId& id) { setClipState(id, std::nullopt, true); }

void ProjectModel::putTag(Tag t) {
    auto id = t.id;
    setTagState(id, t, true);
}
void ProjectModel::eraseTag(const TagId& id) { setTagState(id, std::nullopt, true); }

void ProjectModel::setClipTags(const ClipId& id, std::set<TagId> tags) { setClipTagsState(id, tags, true); }

void ProjectModel::setClipMetadata(const ClipId& id, std::shared_ptr<const ClipMetadata> md) {
    setClipMetadataState(id, md, true);
}

void ProjectModel::setConfig(OrganizationConfig c) {
    if (c == config_) return;
    deltas_.push_back(ConfigDelta{"config", config_, c});
    config_ = std::move(c);
}

// ---- Loading -------------------------------------------------------------------

void ProjectModel::loadFolder(Folder f) {
    auto id = f.id;
    setFolderState(id, f, false);
}
void ProjectModel::loadClip(Clip c) {
    auto id = c.id;
    setClipState(id, c, false);
}
void ProjectModel::loadTag(Tag t) {
    auto id = t.id;
    setTagState(id, t, false);
}
void ProjectModel::loadClipTags(const ClipId& id, std::set<TagId> tags) { setClipTagsState(id, tags, false); }
void ProjectModel::loadClipMetadata(const ClipId& id, std::shared_ptr<const ClipMetadata> md) {
    setClipMetadataState(id, md, false);
}

// ---- Delta log ------------------------------------------------------------------

DeltaList ProjectModel::takeDeltas() {
    DeltaList out;
    out.swap(deltas_);
    return out;
}

void ProjectModel::restore(const Delta& d, bool useAfter) {
    std::visit(
        [&](const auto& delta) {
            using T = std::decay_t<decltype(delta)>;
            const auto& target = useAfter ? delta.after : delta.before;
            if constexpr (std::is_same_v<T, FolderDelta>) {
                std::optional<Folder> next = target;
                if (next) {
                    if (const Folder* cur = folder(delta.id)) next->physicalPath = cur->physicalPath;
                }
                setFolderState(delta.id, next, true);
            } else if constexpr (std::is_same_v<T, ClipDelta>) {
                std::optional<Clip> next = target;
                if (next) {
                    if (const Clip* cur = clip(delta.id)) {
                        next->filePath = cur->filePath;
                        next->status = cur->status;
                        next->fileHash = cur->fileHash;
                        next->fileSize = cur->fileSize;
                    }
                }
                setClipState(delta.id, next, true);
            } else if constexpr (std::is_same_v<T, TagDelta>) {
                setTagState(delta.id, target, true);
            } else if constexpr (std::is_same_v<T, ClipTagsDelta>) {
                setClipTagsState(delta.id, target, true);
            } else if constexpr (std::is_same_v<T, ClipMetadataDelta>) {
                setClipMetadataState(delta.id, target, true);
            } else if constexpr (std::is_same_v<T, ConfigDelta>) {
                if (target) setConfig(*target);
            }
        },
        d);
}

void ProjectModel::revert(const DeltaList& deltas) {
    for (auto it = deltas.rbegin(); it != deltas.rend(); ++it) restore(*it, false);
}

void ProjectModel::reapply(const DeltaList& deltas) {
    for (const auto& d : deltas) restore(d, true);
}

} // namespace vo
