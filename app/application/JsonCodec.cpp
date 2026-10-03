#include "app/application/JsonCodec.h"

#include <map>
#include <set>

#include "core/util/PathUtil.h"

namespace vo::codec {

json snapshot(const ProjectSession& session) {
    std::lock_guard lock(session.mutex());
    const ProjectModel& m = session.model();
    json j;
    j["project"] = {{"id", m.info.id},
                    {"name", m.info.name},
                    {"rootPath", m.info.rootPath},
                    {"projectFile", m.info.projectFile}};
    j["rootFolderId"] = m.rootFolderId();

    json folders = json::array();
    for (const auto& [id, f] : m.folders()) {
        json fj = {{"id", id},
                   {"parentId", f.parentId},
                   {"name", f.name},
                   {"origin", toString(f.origin)},
                   {"path", m.folderPath(id)}};
        fj["physicalPath"] = f.physicalPath ? json(*f.physicalPath) : json(nullptr);
        folders.push_back(std::move(fj));
    }
    j["folders"] = std::move(folders);

    std::map<TagId, size_t> tagCounts;
    size_t missing = 0;
    json clips = json::array();
    for (const auto& [id, c] : m.clips()) {
        json cj = {{"id", id},
                   {"folderId", c.folderId},
                   {"title", c.title},
                   {"extension", c.extension},
                   {"originalFilename", c.originalFilename},
                   {"filePath", c.filePath},
                   {"fileSize", c.fileSize},
                   {"status", toString(c.status)},
                   {"placementOverride", c.placementOverride},
                   {"titleOverride", c.titleOverride}};
        json tags = json::array();
        for (const auto& t : m.clipTags(id)) {
            tags.push_back(t);
            ++tagCounts[t];
        }
        cj["tagIds"] = std::move(tags);
        json meta = json::object(), display = json::object();
        if (auto md = m.metadata(id)) {
            for (const auto& [cat, value] : md->normalized) {
                meta[categoryKey(cat)] = value;
                display[categoryKey(cat)] = displayValue(cat, value);
            }
        }
        cj["metadata"] = std::move(meta);
        cj["display"] = std::move(display);
        if (c.status == ClipStatus::Missing) ++missing;
        clips.push_back(std::move(cj));
    }
    j["clips"] = std::move(clips);

    json tags = json::array();
    for (const auto& [id, t] : m.tags()) tags.push_back({{"id", id}, {"name", t.name}, {"count", tagCounts[id]}});
    j["tags"] = std::move(tags);

    json criteria = json::array();
    for (auto c : m.config().criteria) criteria.push_back(categoryKey(c));
    j["config"] = {{"criteria", criteria}, {"namingTemplate", m.config().namingTemplate}};

    const UndoManager& u = session.undoManager();
    j["undo"] = {{"canUndo", u.canUndo()},
                 {"canRedo", u.canRedo()},
                 {"undoDescription", u.undoDescription()},
                 {"redoDescription", u.redoDescription()}};

    ChangeSet cs = computeChanges(m);
    j["pendingChangeCount"] = cs.size();
    j["missingCount"] = missing;

    json cats = json::array();
    for (auto c : allMetadataCategories()) cats.push_back({{"key", categoryKey(c)}, {"name", categoryDisplayName(c)}});
    j["categories"] = std::move(cats);
    j["namingVariables"] = NamingEngine::supportedVariables();
    return j;
}

json clipDetails(const ProjectModel& m, const ClipId& id) {
    const Clip* c = m.clip(id);
    if (!c) throw UserError("Clip not found");
    json raw = json::array();
    if (auto md = m.metadata(id))
        for (const auto& e : md->raw) raw.push_back({{"key", e.key}, {"value", e.value}, {"source", e.source}});
    return {{"id", id},
            {"raw", raw},
            {"fileHash", c->fileHash},
            {"absolutePath", path::absolute(m.info.rootPath, c->filePath)},
            {"intendedPath", m.clipIntendedPath(id)},
            {"folderPath", m.folderPath(c->folderId)}};
}

namespace {
json changeJson(const Change& c) {
    return {{"type", toString(c.type)},
            {"origin", toString(c.origin)},
            {"clipId", c.clipId},
            {"folderId", c.folderId},
            {"source", c.sourcePath},
            {"destination", c.destinationPath},
            {"oldName", c.oldName},
            {"newName", c.newName},
            {"moves", c.moves},
            {"renames", c.renames}};
}
} // namespace

json changes(const ProjectModel& m, const ChangeSet& cs) {
    json folders = json::array(), clips = json::array(), skipped = json::array();
    size_t creates = 0, folderMoves = 0, moves = 0, renames = 0;
    for (const auto& c : cs.folderChanges) {
        folders.push_back(changeJson(c));
        if (c.type == ChangeType::CreateFolder) ++creates;
        else ++folderMoves;
    }
    for (const auto& c : cs.clipChanges) {
        clips.push_back(changeJson(c));
        moves += c.moves;
        renames += c.renames;
    }
    for (const auto& id : cs.skippedMissing)
        skipped.push_back({{"clipId", id}, {"path", m.clip(id)->filePath}, {"intendedPath", m.clipIntendedPath(id)}});
    return {{"folders", folders},
            {"clips", clips},
            {"skippedMissing", skipped},
            {"counts", {{"folderCreates", creates}, {"folderMoves", folderMoves}, {"clipMoves", moves}, {"clipRenames", renames}}}};
}

json preflight(const PreflightReport& r) {
    json checks = json::array(), issues = json::array();
    for (const auto& c : r.checks) checks.push_back({{"description", c.description}, {"passed", c.passed}});
    for (const auto& i : r.issues)
        issues.push_back({{"severity", toString(i.severity)},
                          {"message", i.message},
                          {"path", i.path},
                          {"clipId", i.clipId},
                          {"folderId", i.folderId}});
    return {{"ok", r.ok},
            {"checks", checks},
            {"issues", issues},
            {"errorCount", r.errorCount()},
            {"warningCount", r.warningCount()},
            {"counts",
             {{"folderCreates", r.folderCreates}, {"folderMoves", r.folderMoves}, {"clipMoves", r.clipMoves}, {"clipRenames", r.clipRenames}}}};
}

json applyOperation(const ApplyOperationResult& op) {
    return {{"type", op.type},
            {"clipId", op.clipId},
            {"folderId", op.folderId},
            {"source", op.source},
            {"destination", op.destination},
            {"success", op.success},
            {"error", op.error},
            {"description", op.description}};
}

json applyResult(const ApplyResult& r) {
    json ops = json::array();
    for (const auto& op : r.operations) ops.push_back(applyOperation(op));
    return {{"state", toString(r.state)},
            {"succeeded", r.succeeded},
            {"failed", r.failed},
            {"operations", ops},
            {"removedDirectories", r.removedDirectories},
            {"preflight", preflight(r.preflight)}};
}

json refreshResult(const RefreshResult& r) {
    return {{"newlyMissing", r.newlyMissing}, {"recovered", r.recovered}, {"detachedFolders", r.detachedFolders}};
}

json searchResult(const SearchResult& r) { return {{"clipIds", r.clips}, {"folderIds", r.folders}}; }

json categoryStats(const ProjectModel& m) {
    json out = json::array();
    for (auto cat : allMetadataCategories()) {
        size_t with = 0;
        std::set<std::string> distinct;
        for (const auto& [id, c] : m.clips()) {
            auto v = m.normalizedValue(id, cat);
            if (v) ++with;
            distinct.insert(groupValue(cat, v));
        }
        out.push_back({{"key", categoryKey(cat)},
                       {"name", categoryDisplayName(cat)},
                       {"clipsWithValue", with},
                       {"distinctValues", distinct.size()}});
    }
    return out;
}

std::vector<MetadataCategory> criteriaFromJson(const json& j) {
    std::vector<MetadataCategory> out;
    if (!j.is_array()) return out;
    for (const auto& k : j) {
        auto c = categoryFromKey(k.get<std::string>());
        if (!c) throw UserError("Unknown metadata category: " + k.get<std::string>());
        if (std::find(out.begin(), out.end(), *c) == out.end()) out.push_back(*c);
    }
    return out;
}

} // namespace vo::codec
