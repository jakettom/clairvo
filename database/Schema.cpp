#include "database/Schema.h"

#include <functional>
#include <vector>

namespace vo::db {

namespace {

// Foreign keys between model tables are DEFERRABLE INITIALLY DEFERRED so a
// command's deltas can be written in any order within one transaction.
const char* kSchemaV1 = R"SQL(
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE projects (
    id         TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    root_path  TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE folders (
    id            TEXT PRIMARY KEY,
    project_id    TEXT NOT NULL REFERENCES projects(id),
    parent_id     TEXT REFERENCES folders(id) DEFERRABLE INITIALLY DEFERRED,
    name          TEXT NOT NULL,
    order_index   INTEGER NOT NULL DEFAULT 0,
    origin        TEXT NOT NULL,
    physical_path TEXT
);
CREATE INDEX idx_folders_parent ON folders(project_id, parent_id);

CREATE TABLE clips (
    id                 TEXT PRIMARY KEY,
    project_id         TEXT NOT NULL REFERENCES projects(id),
    folder_id          TEXT NOT NULL REFERENCES folders(id) DEFERRABLE INITIALLY DEFERRED,
    file_hash          TEXT NOT NULL DEFAULT '',
    file_path          TEXT NOT NULL,
    file_size          INTEGER NOT NULL DEFAULT 0,
    original_filename  TEXT NOT NULL,
    title              TEXT NOT NULL,
    extension          TEXT NOT NULL,
    status             TEXT NOT NULL,
    placement_override INTEGER NOT NULL DEFAULT 0,
    title_override     INTEGER NOT NULL DEFAULT 0,
    created_at         INTEGER NOT NULL,
    updated_at         INTEGER NOT NULL
);
CREATE INDEX idx_clips_folder ON clips(project_id, folder_id);
CREATE INDEX idx_clips_path ON clips(project_id, file_path);
CREATE INDEX idx_clips_hash ON clips(file_hash);

CREATE TABLE raw_metadata (
    clip_id   TEXT NOT NULL REFERENCES clips(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    seq       INTEGER NOT NULL,
    raw_key   TEXT NOT NULL,
    raw_value TEXT NOT NULL,
    source    TEXT NOT NULL,
    PRIMARY KEY (clip_id, seq)
);

CREATE TABLE normalized_metadata (
    clip_id  TEXT NOT NULL REFERENCES clips(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    category TEXT NOT NULL,
    value    TEXT NOT NULL,
    PRIMARY KEY (clip_id, category)
);
CREATE INDEX idx_normalized_category ON normalized_metadata(category, value);

CREATE TABLE tags (
    id         TEXT PRIMARY KEY,
    project_id TEXT NOT NULL REFERENCES projects(id),
    name       TEXT NOT NULL
);
CREATE INDEX idx_tags_name ON tags(project_id, name);

CREATE TABLE clip_tags (
    clip_id TEXT NOT NULL REFERENCES clips(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    tag_id  TEXT NOT NULL REFERENCES tags(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    PRIMARY KEY (clip_id, tag_id)
);
CREATE INDEX idx_clip_tags_tag ON clip_tags(tag_id);

CREATE TABLE organization_configs (
    id              TEXT PRIMARY KEY,
    project_id      TEXT NOT NULL REFERENCES projects(id),
    sort_fields     TEXT NOT NULL,
    naming_template TEXT NOT NULL
);

CREATE TABLE apply_batches (
    id          TEXT PRIMARY KEY,
    started_at  INTEGER NOT NULL,
    finished_at INTEGER,
    state       TEXT NOT NULL
);

CREATE TABLE apply_operations (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    batch_id         TEXT NOT NULL REFERENCES apply_batches(id),
    seq              INTEGER NOT NULL,
    type             TEXT NOT NULL,
    clip_id          TEXT,
    folder_id        TEXT,
    source_path      TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    status           TEXT NOT NULL,
    error            TEXT,
    updated_at       INTEGER NOT NULL
);
CREATE INDEX idx_apply_ops_status ON apply_operations(status);
)SQL";

} // namespace

void migrate(Database& db) {
    // migrations[i] upgrades from version i to i+1.
    const std::vector<std::function<void(Database&)>> migrations = {
        [](Database& d) { d.exec(kSchemaV1); },
    };
    int version = db.userVersion();
    if (version > kCurrentSchemaVersion)
        throw SqliteError("This project was created by a newer version of Clairvo (schema v" + std::to_string(version) +
                          ") and cannot be opened.");
    while (version < kCurrentSchemaVersion) {
        Transaction tx(db);
        migrations[static_cast<size_t>(version)](db);
        ++version;
        db.setUserVersion(version);
        db.prepare("INSERT INTO meta(key, value) VALUES('schema_version', ?1) "
                   "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
            .bind(1, std::to_string(version))
            .run();
        tx.commit();
    }
}

} // namespace vo::db
