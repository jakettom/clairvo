#include "core/filesystem/MissingFileDetector.h"

#include <algorithm>

#include "core/util/PathUtil.h"

namespace vo {

RefreshResult refreshPhysicalState(ProjectModel& m, FileSystem& fs) {
    RefreshResult result;
    const std::string& root = m.info.rootPath;

    std::vector<ClipId> clipIds;
    clipIds.reserve(m.clips().size());
    for (const auto& [id, c] : m.clips()) clipIds.push_back(id);
    std::sort(clipIds.begin(), clipIds.end());
    for (const auto& id : clipIds) {
        Clip c = *m.clip(id);
        FileInfo fi = fs.info(path::absolute(root, c.filePath));
        ClipStatus status = fi.isRegularFile ? ClipStatus::Available : ClipStatus::Missing;
        if (status == c.status) continue;
        if (status == ClipStatus::Missing) result.newlyMissing.push_back(id);
        else result.recovered.push_back(id);
        c.status = status;
        c.updatedAt = nowUnixSeconds();
        m.putClip(c);
    }

    std::vector<FolderId> folderIds;
    for (const auto& [id, f] : m.folders())
        if (f.physicalPath && !f.parentId.empty()) folderIds.push_back(id);
    std::sort(folderIds.begin(), folderIds.end());
    for (const auto& id : folderIds) {
        Folder f = *m.folder(id);
        if (fs.isDirectory(path::absolute(root, *f.physicalPath))) continue;
        f.physicalPath.reset();
        m.putFolder(f);
        result.detachedFolders.push_back(id);
    }
    return result;
}

} // namespace vo
