#pragma once

#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "core/model/Types.h"

namespace vo {

// ---------------------------------------------------------------------------
// Deltas
//
// Every primitive mutation of the model records a before/after pair. A command
// is any sequence of primitive mutations; its recorded deltas are what the
// UndoManager reverts/reapplies and what the persistence layer writes to
// SQLite. This gives every command execute()/undo() semantics without a
// hand-written inverse per command type.
// ---------------------------------------------------------------------------
template <class T>
struct StateDelta {
    Id id;
    std::optional<T> before;
    std::optional<T> after;
};

using FolderDelta = StateDelta<Folder>;
using ClipDelta = StateDelta<Clip>;
using TagDelta = StateDelta<Tag>;
using ClipTagsDelta = StateDelta<std::set<TagId>>;                         // id = clip id
using ClipMetadataDelta = StateDelta<std::shared_ptr<const ClipMetadata>>; // id = clip id
using ConfigDelta = StateDelta<OrganizationConfig>;                        // id unused

using Delta = std::variant<FolderDelta, ClipDelta, TagDelta, ClipTagsDelta, ClipMetadataDelta, ConfigDelta>;
using DeltaList = std::vector<Delta>;

// ---------------------------------------------------------------------------
// ProjectModel — the authoritative virtual hierarchy (spec §3.2).
// ---------------------------------------------------------------------------
class ProjectModel {
public:
    ProjectModel() = default;
    ProjectModel(const ProjectModel&) = delete;
    ProjectModel& operator=(const ProjectModel&) = delete;

    ProjectInfo info;

    const FolderId& rootFolderId() const { return rootFolderId_; }

    // ---- Reads -------------------------------------------------------------
    const Folder* folder(const FolderId& id) const;
    const Clip* clip(const ClipId& id) const;
    const Tag* tag(const TagId& id) const;
    const Tag* tagByName(const std::string& name) const; // case-insensitive

    const std::unordered_map<FolderId, Folder>& folders() const { return folders_; }
    const std::unordered_map<ClipId, Clip>& clips() const { return clips_; }
    const std::unordered_map<TagId, Tag>& tags() const { return tags_; }

    std::vector<FolderId> childFolders(const FolderId& id) const; // natural name order
    std::vector<ClipId> clipsInFolder(const FolderId& id) const;  // natural title order
    size_t childFolderCount(const FolderId& id) const;
    size_t clipCount(const FolderId& id) const;

    const std::set<TagId>& clipTags(const ClipId& id) const;
    std::shared_ptr<const ClipMetadata> metadata(const ClipId& id) const;
    std::optional<std::string> normalizedValue(const ClipId& id, MetadataCategory c) const;

    const OrganizationConfig& config() const { return config_; }

    // Intended (virtual) paths relative to the root.
    std::string folderPath(const FolderId& id) const;
    std::vector<std::string> folderPathComponents(const FolderId& id) const;
    std::string clipIntendedPath(const ClipId& id) const;

    bool isAncestorOrSelf(const FolderId& ancestor, const FolderId& folder) const;
    std::vector<FolderId> subtreeFolders(const FolderId& id) const; // pre-order, includes id
    std::vector<ClipId> subtreeClips(const FolderId& id) const;
    std::optional<FolderId> childFolderNamed(const FolderId& parent, const std::string& name) const;
    std::optional<FolderId> folderWithPhysicalPath(const std::string& relPath) const;
    std::optional<ClipId> clipWithFilePath(const std::string& relPath) const; // case-insensitive

    // ---- Primitive mutations (recorded as deltas) ---------------------------
    void putFolder(Folder f);
    void eraseFolder(const FolderId& id);
    void putClip(Clip c);
    void eraseClip(const ClipId& id);
    void putTag(Tag t);
    void eraseTag(const TagId& id);
    void setClipTags(const ClipId& id, std::set<TagId> tags);
    void setClipMetadata(const ClipId& id, std::shared_ptr<const ClipMetadata> md);
    void setConfig(OrganizationConfig c);
    void setRootFolderId(const FolderId& id) { rootFolderId_ = id; }

    // ---- Delta log ---------------------------------------------------------
    DeltaList takeDeltas();
    bool hasPendingDeltas() const { return !deltas_.empty(); }
    // Restores the "before" states of `deltas` in reverse order (undo) or the
    // "after" states in order (redo). Physical fields (file path, status, hash,
    // folder physical path) keep their current values: undo/redo is virtual
    // and never claims a filesystem change (spec §32).
    void revert(const DeltaList& deltas);
    void reapply(const DeltaList& deltas);

    // ---- Loading (no deltas recorded) ---------------------------------------
    void loadFolder(Folder f);
    void loadClip(Clip c);
    void loadTag(Tag t);
    void loadClipTags(const ClipId& id, std::set<TagId> tags);
    void loadClipMetadata(const ClipId& id, std::shared_ptr<const ClipMetadata> md);
    void loadConfig(OrganizationConfig c) { config_ = std::move(c); }

private:
    void setFolderState(const FolderId& id, const std::optional<Folder>& f, bool record);
    void setClipState(const ClipId& id, const std::optional<Clip>& c, bool record);
    void setTagState(const TagId& id, const std::optional<Tag>& t, bool record);
    void setClipTagsState(const ClipId& id, const std::optional<std::set<TagId>>& t, bool record);
    void setClipMetadataState(const ClipId& id, const std::optional<std::shared_ptr<const ClipMetadata>>& m,
                              bool record);
    void restore(const Delta& d, bool useAfter);

    FolderId rootFolderId_;
    std::unordered_map<FolderId, Folder> folders_;
    std::unordered_map<ClipId, Clip> clips_;
    std::unordered_map<TagId, Tag> tags_;
    std::unordered_map<ClipId, std::set<TagId>> clipTags_;
    std::unordered_map<ClipId, std::shared_ptr<const ClipMetadata>> metadata_;
    OrganizationConfig config_;

    // Indexes
    std::unordered_map<FolderId, std::unordered_set<FolderId>> childFolders_;
    std::unordered_map<FolderId, std::unordered_set<ClipId>> folderClips_;
    std::unordered_map<std::string, ClipId> clipByLowerPath_;

    DeltaList deltas_;
};

} // namespace vo
