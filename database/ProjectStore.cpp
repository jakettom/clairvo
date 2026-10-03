#include "database/ProjectStore.h"

#include <set>
#include <sys/stat.h>

#include "core/util/StringUtil.h"
#include "database/Schema.h"

namespace vo {

namespace {

std::string encodeCriteria(const std::vector<MetadataCategory>& criteria) {
    std::vector<std::string> keys;
    for (auto c : criteria) keys.push_back(categoryKey(c));
    return join(keys, ",");
}

std::vector<MetadataCategory> decodeCriteria(const std::string& s) {
    std::vector<MetadataCategory> out;
    if (s.empty()) return out;
    for (const auto& k : split(s, ','))
        if (auto c = categoryFromKey(k)) out.push_back(*c);
    return out;
}

} // namespace

void ProjectStore::create(const std::string& projectFile, const ProjectInfo& info, const Folder& root,
                          const OrganizationConfig& config) {
    struct stat st{};
    if (::stat(projectFile.c_str(), &st) == 0)
        throw UserError("A project file already exists at " + projectFile + ". Open it instead.");
    db_.open(projectFile, true);
    db::migrate(db_);
    projectId_ = info.id;
    db::Transaction tx(db_);
    db_.prepare("INSERT INTO projects(id, name, root_path, created_at, updated_at) VALUES(?1, ?2, ?3, ?4, ?5)")
        .bind(1, info.id)
        .bind(2, info.name)
        .bind(3, info.rootPath)
        .bind(4, info.createdAt)
        .bind(5, info.updatedAt)
        .run();
    db_.prepare("INSERT INTO folders(id, project_id, parent_id, name, order_index, origin, physical_path) "
                "VALUES(?1, ?2, NULL, ?3, 0, ?4, '')")
        .bind(1, root.id)
        .bind(2, info.id)
        .bind(3, root.name)
        .bind(4, toString(root.origin))
        .run();
    db_.prepare("INSERT INTO organization_configs(id, project_id, sort_fields, naming_template) VALUES(?1, ?2, ?3, ?4)")
        .bind(1, info.id)
        .bind(2, info.id)
        .bind(3, encodeCriteria(config.criteria))
        .bind(4, config.namingTemplate)
        .run();
    tx.commit();
}

void ProjectStore::open(const std::string& projectFile) {
    db_.open(projectFile, false);
    db::migrate(db_);
    auto st = db_.prepare("SELECT id FROM projects LIMIT 1");
    if (!st.step()) throw UserError("The project file is damaged: no project record found");
    projectId_ = st.text(0);
}

void ProjectStore::load(ProjectModel& m) {
    {
        auto st = db_.prepare("SELECT id, name, root_path, created_at, updated_at FROM projects WHERE id = ?1");
        st.bind(1, projectId_);
        if (!st.step()) throw UserError("The project file is damaged: no project record found");
        m.info.id = st.text(0);
        m.info.name = st.text(1);
        m.info.rootPath = st.text(2);
        m.info.createdAt = st.integer(3);
        m.info.updatedAt = st.integer(4);
    }
    {
        auto st = db_.prepare("SELECT id, parent_id, name, order_index, origin, physical_path FROM folders "
                              "WHERE project_id = ?1");
        st.bind(1, projectId_);
        while (st.step()) {
            Folder f;
            f.id = st.text(0);
            f.parentId = st.optionalText(1).value_or("");
            f.name = st.text(2);
            f.orderIndex = static_cast<int>(st.integer(3));
            f.origin = folderOriginFromString(st.text(4));
            f.physicalPath = st.optionalText(5);
            if (f.parentId.empty()) f.physicalPath = "";
            m.loadFolder(f);
        }
    }
    if (m.rootFolderId().empty()) throw UserError("The project file is damaged: no root folder");
    {
        auto st = db_.prepare("SELECT id, folder_id, file_hash, file_path, file_size, original_filename, title, extension, "
                              "status, placement_override, title_override, created_at, updated_at FROM clips "
                              "WHERE project_id = ?1");
        st.bind(1, projectId_);
        while (st.step()) {
            Clip c;
            c.id = st.text(0);
            c.folderId = st.text(1);
            c.fileHash = st.text(2);
            c.filePath = st.text(3);
            c.fileSize = st.integer(4);
            c.originalFilename = st.text(5);
            c.title = st.text(6);
            c.extension = st.text(7);
            c.status = clipStatusFromString(st.text(8));
            c.placementOverride = st.integer(9) != 0;
            c.titleOverride = st.integer(10) != 0;
            c.createdAt = st.integer(11);
            c.updatedAt = st.integer(12);
            if (!m.folder(c.folderId)) c.folderId = m.rootFolderId(); // defensive: never lose a clip
            m.loadClip(c);
        }
    }
    {
        std::unordered_map<ClipId, std::shared_ptr<ClipMetadata>> md;
        auto raw = db_.prepare("SELECT r.clip_id, r.raw_key, r.raw_value, r.source FROM raw_metadata r "
                               "JOIN clips c ON c.id = r.clip_id WHERE c.project_id = ?1 ORDER BY r.clip_id, r.seq");
        raw.bind(1, projectId_);
        while (raw.step()) {
            auto& entry = md[raw.text(0)];
            if (!entry) entry = std::make_shared<ClipMetadata>();
            entry->raw.push_back({raw.text(1), raw.text(2), raw.text(3)});
        }
        auto norm = db_.prepare("SELECT n.clip_id, n.category, n.value FROM normalized_metadata n "
                                "JOIN clips c ON c.id = n.clip_id WHERE c.project_id = ?1");
        norm.bind(1, projectId_);
        while (norm.step()) {
            auto cat = categoryFromKey(norm.text(1));
            if (!cat) continue;
            auto& entry = md[norm.text(0)];
            if (!entry) entry = std::make_shared<ClipMetadata>();
            entry->normalized[*cat] = norm.text(2);
        }
        for (auto& [id, entry] : md)
            if (m.clip(id)) m.loadClipMetadata(id, entry);
    }
    {
        auto st = db_.prepare("SELECT id, name FROM tags WHERE project_id = ?1");
        st.bind(1, projectId_);
        while (st.step()) m.loadTag(Tag{st.text(0), st.text(1)});
        std::unordered_map<ClipId, std::set<TagId>> ct;
        auto links = db_.prepare("SELECT ct.clip_id, ct.tag_id FROM clip_tags ct JOIN clips c ON c.id = ct.clip_id "
                                 "WHERE c.project_id = ?1");
        links.bind(1, projectId_);
        while (links.step()) ct[links.text(0)].insert(links.text(1));
        for (auto& [id, tags] : ct)
            if (m.clip(id)) m.loadClipTags(id, tags);
    }
    {
        auto st = db_.prepare("SELECT sort_fields, naming_template FROM organization_configs WHERE project_id = ?1");
        st.bind(1, projectId_);
        OrganizationConfig cfg;
        if (st.step()) {
            cfg.criteria = decodeCriteria(st.text(0));
            cfg.namingTemplate = st.text(1);
        }
        m.loadConfig(cfg);
    }
}

void ProjectStore::updateProjectRow(const ProjectInfo& info) {
    db_.prepare("UPDATE projects SET name = ?2, root_path = ?3, updated_at = ?4 WHERE id = ?1")
        .bind(1, info.id)
        .bind(2, info.name)
        .bind(3, info.rootPath)
        .bind(4, nowUnixSeconds())
        .run();
}

void ProjectStore::persist(const ProjectModel& model, const DeltaList& deltas) {
    if (deltas.empty()) return;
    db::Transaction tx(db_);
    writeDeltas(model, deltas);
    tx.commit();
}

void ProjectStore::writeDeltas(const ProjectModel& model, const DeltaList& deltas) {
    std::set<Id> folders, clips, tags, clipTags, clipMeta;
    bool config = false;
    for (const auto& d : deltas) {
        std::visit(
            [&](const auto& delta) {
                using T = std::decay_t<decltype(delta)>;
                if constexpr (std::is_same_v<T, FolderDelta>) folders.insert(delta.id);
                else if constexpr (std::is_same_v<T, ClipDelta>) clips.insert(delta.id);
                else if constexpr (std::is_same_v<T, TagDelta>) tags.insert(delta.id);
                else if constexpr (std::is_same_v<T, ClipTagsDelta>) clipTags.insert(delta.id);
                else if constexpr (std::is_same_v<T, ClipMetadataDelta>) clipMeta.insert(delta.id);
                else if constexpr (std::is_same_v<T, ConfigDelta>) config = true;
            },
            d);
    }
    for (const auto& id : folders) writeFolder(model, id);
    for (const auto& id : clips) writeClip(model, id);
    for (const auto& id : tags) writeTag(model, id);
    for (const auto& id : clipMeta) writeClipMetadata(model, id);
    for (const auto& id : clipTags) writeClipTags(model, id);
    if (config) writeConfig(model);
    db_.prepare("UPDATE projects SET updated_at = ?2 WHERE id = ?1").bind(1, projectId_).bind(2, nowUnixSeconds()).run();
}

void ProjectStore::writeFolder(const ProjectModel& model, const FolderId& id) {
    const Folder* f = model.folder(id);
    if (!f) {
        db_.prepare("DELETE FROM folders WHERE id = ?1").bind(1, id).run();
        return;
    }
    auto st = db_.prepare(
        "INSERT INTO folders(id, project_id, parent_id, name, order_index, origin, physical_path) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7) ON CONFLICT(id) DO UPDATE SET parent_id = excluded.parent_id, "
        "name = excluded.name, order_index = excluded.order_index, origin = excluded.origin, "
        "physical_path = excluded.physical_path");
    st.bind(1, f->id).bind(2, projectId_);
    if (f->parentId.empty()) st.bindNull(3);
    else st.bind(3, f->parentId);
    st.bind(4, f->name).bind(5, f->orderIndex).bind(6, toString(f->origin)).bind(7, f->physicalPath);
    st.run();
}

void ProjectStore::writeClip(const ProjectModel& model, const ClipId& id) {
    const Clip* c = model.clip(id);
    if (!c) {
        db_.prepare("DELETE FROM clips WHERE id = ?1").bind(1, id).run();
        return;
    }
    db_.prepare("INSERT INTO clips(id, project_id, folder_id, file_hash, file_path, file_size, original_filename, title, "
                "extension, status, placement_override, title_override, created_at, updated_at) "
                "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14) ON CONFLICT(id) DO UPDATE SET "
                "folder_id = excluded.folder_id, file_hash = excluded.file_hash, file_path = excluded.file_path, "
                "file_size = excluded.file_size, original_filename = excluded.original_filename, "
                "title = excluded.title, extension = excluded.extension, status = excluded.status, "
                "placement_override = excluded.placement_override, title_override = excluded.title_override, "
                "updated_at = excluded.updated_at")
        .bind(1, c->id)
        .bind(2, projectId_)
        .bind(3, c->folderId)
        .bind(4, c->fileHash)
        .bind(5, c->filePath)
        .bind(6, c->fileSize)
        .bind(7, c->originalFilename)
        .bind(8, c->title)
        .bind(9, c->extension)
        .bind(10, toString(c->status))
        .bind(11, c->placementOverride)
        .bind(12, c->titleOverride)
        .bind(13, c->createdAt)
        .bind(14, c->updatedAt)
        .run();
}

void ProjectStore::writeTag(const ProjectModel& model, const TagId& id) {
    const Tag* t = model.tag(id);
    if (!t) {
        db_.prepare("DELETE FROM tags WHERE id = ?1").bind(1, id).run();
        return;
    }
    db_.prepare("INSERT INTO tags(id, project_id, name) VALUES(?1, ?2, ?3) "
                "ON CONFLICT(id) DO UPDATE SET name = excluded.name")
        .bind(1, t->id)
        .bind(2, projectId_)
        .bind(3, t->name)
        .run();
}

void ProjectStore::writeClipTags(const ProjectModel& model, const ClipId& id) {
    db_.prepare("DELETE FROM clip_tags WHERE clip_id = ?1").bind(1, id).run();
    if (!model.clip(id)) return;
    auto ins = db_.prepare("INSERT INTO clip_tags(clip_id, tag_id) VALUES(?1, ?2)");
    for (const auto& tag : model.clipTags(id)) {
        if (!model.tag(tag)) continue;
        ins.bind(1, id).bind(2, tag).run();
        ins.reset();
    }
}

void ProjectStore::writeClipMetadata(const ProjectModel& model, const ClipId& id) {
    db_.prepare("DELETE FROM raw_metadata WHERE clip_id = ?1").bind(1, id).run();
    db_.prepare("DELETE FROM normalized_metadata WHERE clip_id = ?1").bind(1, id).run();
    if (!model.clip(id)) return;
    auto md = model.metadata(id);
    if (!md) return;
    auto raw = db_.prepare("INSERT INTO raw_metadata(clip_id, seq, raw_key, raw_value, source) VALUES(?1, ?2, ?3, ?4, ?5)");
    int seq = 0;
    for (const auto& e : md->raw) {
        raw.bind(1, id).bind(2, seq++).bind(3, e.key).bind(4, e.value).bind(5, e.source).run();
        raw.reset();
    }
    auto norm = db_.prepare("INSERT INTO normalized_metadata(clip_id, category, value) VALUES(?1, ?2, ?3)");
    for (const auto& [cat, value] : md->normalized) {
        norm.bind(1, id).bind(2, categoryKey(cat)).bind(3, value).run();
        norm.reset();
    }
}

void ProjectStore::writeConfig(const ProjectModel& model) {
    db_.prepare("INSERT INTO organization_configs(id, project_id, sort_fields, naming_template) VALUES(?1, ?1, ?2, ?3) "
                "ON CONFLICT(id) DO UPDATE SET sort_fields = excluded.sort_fields, "
                "naming_template = excluded.naming_template")
        .bind(1, projectId_)
        .bind(2, encodeCriteria(model.config().criteria))
        .bind(3, model.config().namingTemplate)
        .run();
}

std::vector<PendingOperation> ProjectStore::pendingOperations() {
    std::vector<PendingOperation> out;
    auto st = db_.prepare("SELECT id, batch_id, type, clip_id, folder_id, source_path, destination_path "
                          "FROM apply_operations WHERE status = 'PENDING' ORDER BY id");
    while (st.step()) {
        PendingOperation op;
        op.id = st.integer(0);
        op.batchId = st.text(1);
        op.type = st.text(2);
        op.clipId = st.optionalText(3).value_or("");
        op.folderId = st.optionalText(4).value_or("");
        op.source = st.text(5);
        op.destination = st.text(6);
        out.push_back(std::move(op));
    }
    return out;
}

std::string ProjectStore::beginBatch() {
    std::string id = generateId();
    db_.prepare("INSERT INTO apply_batches(id, started_at, state) VALUES(?1, ?2, 'APPLYING')")
        .bind(1, id)
        .bind(2, nowUnixSeconds())
        .run();
    return id;
}

int64_t ProjectStore::recordPending(const std::string& batchId, int seq, const std::string& type, const ClipId& clip,
                                    const FolderId& folder, const std::string& source, const std::string& destination) {
    auto st = db_.prepare("INSERT INTO apply_operations(batch_id, seq, type, clip_id, folder_id, source_path, "
                          "destination_path, status, updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, 'PENDING', ?8)");
    st.bind(1, batchId).bind(2, seq).bind(3, type);
    if (clip.empty()) st.bindNull(4);
    else st.bind(4, clip);
    if (folder.empty()) st.bindNull(5);
    else st.bind(5, folder);
    st.bind(6, source).bind(7, destination).bind(8, nowUnixSeconds());
    st.run();
    return db_.lastInsertRowId();
}

void ProjectStore::recordResult(int64_t opId, bool success, const std::string& error, const DeltaList& deltas) {
    db::Transaction tx(db_);
    if (model_ && !deltas.empty()) writeDeltas(*model_, deltas);
    auto st = db_.prepare("UPDATE apply_operations SET status = ?2, error = ?3, updated_at = ?4 WHERE id = ?1");
    st.bind(1, opId).bind(2, std::string(success ? "SUCCESS" : "FAILED"));
    if (error.empty()) st.bindNull(3);
    else st.bind(3, error);
    st.bind(4, nowUnixSeconds()).run();
    tx.commit();
}

void ProjectStore::finishBatch(const std::string& batchId, const std::string& state) {
    db_.prepare("UPDATE apply_batches SET state = ?2, finished_at = ?3 WHERE id = ?1")
        .bind(1, batchId)
        .bind(2, state)
        .bind(3, nowUnixSeconds())
        .run();
}

} // namespace vo
