#include "core/organization/OrganizationEngine.h"

#include <algorithm>
#include <set>

#include "core/commands/EditCommands.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

std::vector<std::string> OrganizationEngine::automaticPlacement(const ProjectModel& m, const ClipId& clip,
                                                                const std::vector<MetadataCategory>& criteria) {
    std::vector<std::string> names;
    names.reserve(criteria.size());
    for (auto c : criteria) names.push_back(groupValue(c, m.normalizedValue(clip, c)));
    return names;
}

std::vector<ClipId> OrganizationEngine::deterministicClipOrder(const ProjectModel& m, std::vector<ClipId> clips) {
    return NamingEngine::numberingOrder(m, std::move(clips));
}

std::map<ClipId, std::string> OrganizationEngine::propose(const ProjectModel& m, const OrganizationConfig& config) {
    std::map<ClipId, std::string> out;
    for (const auto& [id, c] : m.clips()) {
        if (c.placementOverride) continue;
        if (config.criteria.empty()) out[id] = path::parent(c.filePath);
        else out[id] = join(automaticPlacement(m, id, config.criteria), "/");
    }
    return out;
}

FolderId OrganizationEngine::ensurePhysicalFolder(ProjectModel& m, const std::string& relDir) {
    if (relDir.empty()) return m.rootFolderId();
    if (auto existing = m.folderWithPhysicalPath(relDir)) return *existing;
    FolderId parent = ensurePhysicalFolder(m, path::parent(relDir));
    std::string name = path::filename(relDir);
    if (auto sibling = m.childFolderNamed(parent, name)) {
        const Folder* f = m.folder(*sibling);
        if (!f->physicalPath) {
            // A virtual folder already sits exactly where this directory is.
            Folder adopt = *f;
            adopt.physicalPath = relDir;
            m.putFolder(adopt);
            return adopt.id;
        }
    }
    Folder f;
    f.id = generateId();
    f.parentId = parent;
    f.name = commands::uniqueFolderName(m, parent, path::sanitizeName(name), "");
    f.orderIndex = static_cast<int>(m.childFolderCount(parent));
    f.origin = FolderOrigin::Imported;
    f.physicalPath = relDir;
    m.putFolder(f);
    return f.id;
}

FolderId OrganizationEngine::ensureAutomaticPath(ProjectModel& m, const std::vector<std::string>& names) {
    FolderId current = m.rootFolderId();
    for (const auto& name : names) {
        if (auto existing = m.childFolderNamed(current, name)) {
            current = *existing;
            continue;
        }
        Folder f;
        f.id = generateId();
        f.parentId = current;
        f.name = name;
        f.orderIndex = static_cast<int>(m.childFolderCount(current));
        f.origin = FolderOrigin::Automatic;
        m.putFolder(f);
        current = f.id;
    }
    return current;
}

FolderId OrganizationEngine::targetFolder(ProjectModel& m, const ClipId& clip,
                                          const std::vector<MetadataCategory>& criteria) {
    if (criteria.empty()) return ensurePhysicalFolder(m, path::parent(m.clip(clip)->filePath));
    return ensureAutomaticPath(m, automaticPlacement(m, clip, criteria));
}

void OrganizationEngine::pruneEmptyFolders(ProjectModel& m) {
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<FolderId> candidates;
        for (const auto& [id, f] : m.folders()) {
            if (f.parentId.empty() || f.origin == FolderOrigin::Manual) continue;
            if (m.clipCount(id) == 0 && m.childFolderCount(id) == 0) candidates.push_back(id);
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& id : candidates) {
            m.eraseFolder(id);
            changed = true;
        }
    }
}

void OrganizationEngine::organize(ProjectModel& m, const OrganizationConfig& config, const DiskOccupancy& occupied) {
    if (auto err = NamingEngine::validateTemplate(config.namingTemplate)) throw UserError(*err);
    m.setConfig(config);

    std::vector<ClipId> eligible;
    for (const auto& [id, c] : m.clips())
        if (!c.placementOverride) eligible.push_back(id);

    for (const auto& id : deterministicClipOrder(m, eligible)) {
        FolderId target = targetFolder(m, id, config.criteria);
        Clip c = *m.clip(id);
        if (c.folderId == target) continue;
        c.folderId = target;
        c.updatedAt = nowUnixSeconds();
        m.putClip(c);
    }

    pruneEmptyFolders(m);
    applyNaming(m, config.namingTemplate, occupied);
}

void OrganizationEngine::applyNaming(ProjectModel& m, const std::string& tmpl, const DiskOccupancy& occupied) {
    std::vector<FolderId> all;
    for (const auto& [id, f] : m.folders()) all.push_back(id);
    NamingEngine::applyNaming(m, tmpl, all, occupied);
}

void OrganizationEngine::resetToAutomatic(ProjectModel& m, const std::vector<ClipId>& clips,
                                          const DiskOccupancy& occupied) {
    const auto& config = m.config();
    std::set<FolderId> touched;
    std::vector<ClipId> ids;
    for (const auto& id : clips)
        if (m.clip(id)) ids.push_back(id);
    for (const auto& id : deterministicClipOrder(m, ids)) {
        Clip c = *m.clip(id);
        c.placementOverride = false;
        c.titleOverride = false;
        FolderId target = targetFolder(m, id, config.criteria);
        touched.insert(target);
        if (c.folderId != target) {
            c.title = commands::uniqueClipTitle(m, target, c.title, c.extension, id);
            c.folderId = target;
        }
        c.updatedAt = nowUnixSeconds();
        m.putClip(c);
    }
    NamingEngine::applyNaming(m, config.namingTemplate, {touched.begin(), touched.end()}, occupied);
    pruneEmptyFolders(m);
}

void OrganizationEngine::resetOrganization(ProjectModel& m) {
    // 1. Every folder that exists on disk goes back to its physical place/name.
    std::vector<FolderId> physical;
    for (const auto& [id, f] : m.folders())
        if (f.physicalPath && !f.parentId.empty()) physical.push_back(id);
    std::sort(physical.begin(), physical.end(), [&](const FolderId& a, const FolderId& b) {
        const auto& pa = *m.folder(a)->physicalPath;
        const auto& pb = *m.folder(b)->physicalPath;
        auto da = std::count(pa.begin(), pa.end(), '/'), db = std::count(pb.begin(), pb.end(), '/');
        if (da != db) return da < db;
        return pa < pb;
    });
    for (const auto& id : physical) {
        Folder f = *m.folder(id);
        std::string dir = *f.physicalPath;
        FolderId parent = ensurePhysicalFolder(m, path::parent(dir));
        std::string name = path::filename(dir);
        if (f.parentId != parent || f.name != name) {
            f.parentId = parent;
            f.name = name;
            m.putFolder(f);
        }
    }

    // 2. Every clip returns to its physical directory with its physical name.
    std::vector<ClipId> ids;
    for (const auto& [id, c] : m.clips()) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto& id : ids) {
        Clip c = *m.clip(id);
        FolderId folder = ensurePhysicalFolder(m, path::parent(c.filePath));
        std::string file = path::filename(c.filePath);
        std::string ext = path::extension(file);
        Clip next = c;
        next.folderId = folder;
        next.title = ext.empty() ? file : file.substr(0, file.size() - ext.size());
        next.extension = ext;
        next.placementOverride = false;
        next.titleOverride = false;
        if (next != c) {
            next.updatedAt = nowUnixSeconds();
            m.putClip(next);
        }
    }

    // 3. Purely virtual folders are now empty; remove them bottom-up.
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<FolderId> candidates;
        for (const auto& [id, f] : m.folders())
            if (!f.parentId.empty() && !f.physicalPath && m.clipCount(id) == 0 && m.childFolderCount(id) == 0)
                candidates.push_back(id);
        std::sort(candidates.begin(), candidates.end());
        for (const auto& id : candidates) {
            m.eraseFolder(id);
            changed = true;
        }
    }
}

} // namespace vo
