#include <gtest/gtest.h>

#include "app/application/ImportService.h"
#include "app/application/ProjectSession.h"
#include "core/commands/EditCommands.h"
#include "database/Schema.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

namespace {

std::vector<std::string> describe(const ProjectModel& m) {
    std::vector<std::string> out;
    for (const auto& [id, f] : m.folders())
        out.push_back("F|" + id + "|" + f.parentId + "|" + f.name + "|" + toString(f.origin) + "|" +
                      f.physicalPath.value_or("<null>"));
    for (const auto& [id, c] : m.clips()) {
        std::string tags;
        for (const auto& t : m.clipTags(id)) tags += t + ",";
        std::string md;
        if (auto meta = m.metadata(id); meta && (!meta->raw.empty() || !meta->normalized.empty())) {
            for (const auto& [k, v] : meta->normalized) md += categoryKey(k) + "=" + v + ";";
            md += std::to_string(meta->raw.size());
        }
        out.push_back("C|" + id + "|" + c.folderId + "|" + c.filePath + "|" + c.title + c.extension + "|" +
                      toString(c.status) + "|" + (c.placementOverride ? "P" : "") + (c.titleOverride ? "T" : "") +
                      "|" + tags + "|" + md);
    }
    for (const auto& [id, t] : m.tags()) out.push_back("T|" + id + "|" + t.name);
    std::string criteria;
    for (auto c : m.config().criteria) criteria += categoryKey(c) + ",";
    out.push_back("CFG|" + criteria + "|" + m.config().namingTemplate);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST(ProjectStore, CreateSaveCloseReopenHasIdenticalLogicalState) {
    TempDir dir;
    dir.write("CameraA/C001.MOV");
    dir.write("CameraA/C002.MOV");
    dir.write("C003.MOV");
    auto extractor = std::make_shared<FakeExtractor>();
    extractor->byFilename["C001.MOV"] = rawFor("Sony", "ILCE-7M4", 3840, 2160, 59.94);
    std::vector<std::string> before;
    std::string projectFile;
    {
        auto s = ProjectSession::create(dir.path(), "My Documentary", fakeDependencies(extractor));
        projectFile = s->model().info.projectFile;
        EXPECT_EQ(projectFile, dir.file("My Documentary.project"));
        ImportService::run(*s, "");
        s->organize(OrganizationConfig{{MetadataCategory::Camera}, "{folder}_{number}"});
        auto tagId = s->edit("tag", [&](ProjectModel& m) {
            auto t = commands::createTag(m, "Interview");
            commands::addTag(m, {m.clips().begin()->first}, t);
            return t;
        });
        (void)tagId;
        before = describe(s->model());
    }
    auto reopened = ProjectSession::open(projectFile, fakeDependencies(extractor));
    EXPECT_EQ(describe(reopened->model()), before);
    EXPECT_EQ(reopened->model().info.name, "My Documentary");
    EXPECT_EQ(reopened->model().clips().size(), 3u);
    EXPECT_FALSE(reopened->undoManager().canUndo()); // session-only history
}

TEST(ProjectStore, UndoIsPersisted) {
    TempDir dir;
    dir.write("C001.MOV");
    std::string projectFile;
    ClipId clip;
    {
        auto s = ProjectSession::create(dir.path(), "P", fakeDependencies());
        projectFile = s->model().info.projectFile;
        ImportService::run(*s, "");
        clip = s->model().clips().begin()->first;
        s->edit("rename", [&](ProjectModel& m) { commands::renameClip(m, clip, "Renamed"); });
        s->undo();
    }
    auto reopened = ProjectSession::open(projectFile, fakeDependencies());
    EXPECT_EQ(reopened->model().clip(clip)->title, "C001");
}

TEST(ProjectStore, FailedEditRollsBack) {
    TempDir dir;
    dir.write("C001.MOV");
    auto s = ProjectSession::create(dir.path(), "P", fakeDependencies());
    ImportService::run(*s, "");
    auto clip = s->model().clips().begin()->first;
    EXPECT_THROW(s->edit("bad", [&](ProjectModel& m) {
        commands::renameClip(m, clip, "Good");
        commands::renameClip(m, clip, "bad/name");
    }),
                 UserError);
    EXPECT_EQ(s->model().clip(clip)->title, "C001");
    EXPECT_FALSE(s->undoManager().canUndo());
}

TEST(ProjectStore, SchemaVersionRecordedAndNewerVersionRejected) {
    TempDir dir;
    std::string projectFile;
    {
        auto s = ProjectSession::create(dir.path(), "P", fakeDependencies());
        projectFile = s->model().info.projectFile;
    }
    {
        db::Database db;
        db.open(projectFile, false);
        EXPECT_EQ(db.userVersion(), db::kCurrentSchemaVersion);
        db.setUserVersion(db::kCurrentSchemaVersion + 1);
    }
    EXPECT_THROW(ProjectSession::open(projectFile, fakeDependencies()), std::exception);
}

TEST(ProjectStore, CreatingOverExistingProjectFails) {
    TempDir dir;
    { auto s = ProjectSession::create(dir.path(), "P", fakeDependencies()); }
    EXPECT_THROW(ProjectSession::create(dir.path(), "P", fakeDependencies()), UserError);
}
