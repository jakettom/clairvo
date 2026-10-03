#pragma once

#include <string>
#include <vector>

#include "core/model/ProjectModel.h"

// Virtual editing commands (spec §77). Each command is a validated sequence of
// primitive ProjectModel mutations; the recorded deltas give it execute/undo
// semantics. Commands never touch the filesystem.
namespace vo::commands {

enum class DeleteFolderBehavior { MoveClipsToParent, RemoveClipsFromProject };

FolderId createFolder(ProjectModel& m, const FolderId& parent, const std::string& name);
void renameFolder(ProjectModel& m, const FolderId& folder, const std::string& name);
void moveFolders(ProjectModel& m, const std::vector<FolderId>& folders, const FolderId& destination);
void deleteFolder(ProjectModel& m, const FolderId& folder, DeleteFolderBehavior behavior);

void moveClips(ProjectModel& m, const std::vector<ClipId>& clips, const FolderId& destination);
void renameClip(ProjectModel& m, const ClipId& clip, const std::string& title);
// Removes clips from the project only; physical files are never deleted (FR-DEL-002).
void removeClips(ProjectModel& m, const std::vector<ClipId>& clips);

TagId createTag(ProjectModel& m, const std::string& name); // returns existing tag on name match
void renameTag(ProjectModel& m, const TagId& tag, const std::string& name);
void deleteTag(ProjectModel& m, const TagId& tag);
void addTag(ProjectModel& m, const std::vector<ClipId>& clips, const TagId& tag);
void removeTag(ProjectModel& m, const std::vector<ClipId>& clips, const TagId& tag);

// Name helpers (case-insensitive uniqueness within a folder).
std::string uniqueClipTitle(const ProjectModel& m, const FolderId& folder, const std::string& desired,
                            const std::string& extension, const ClipId& exclude);
std::string uniqueFolderName(const ProjectModel& m, const FolderId& parent, const std::string& desired,
                             const FolderId& exclude);
bool clipTitleTaken(const ProjectModel& m, const FolderId& folder, const std::string& title,
                    const std::string& extension, const ClipId& exclude);

} // namespace vo::commands
