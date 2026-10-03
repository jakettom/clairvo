#include <gtest/gtest.h>

#include "app/application/ImportService.h"
#include "app/application/ProjectSession.h"
#include "app/application/SearchService.h"
#include "core/commands/EditCommands.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

// Roadmap §41 "Definition of Done" scenario end-to-end through the session.
TEST(Workflow, DefinitionOfDoneScenario) {
    TempDir dir;
    auto extractor = std::make_shared<FakeExtractor>();
    for (int i = 1; i <= 40; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "C%03d.MOV", i);
        std::string sub = i <= 20 ? "CardA/" : "CardB/";
        dir.write(sub + name, name);
        bool sony = i % 2 == 0;
        extractor->byFilename[name] = rawFor(sony ? "Sony" : "Apple", sony ? "ILCE-7M4" : "iPhone 15 Pro",
                                             i % 3 == 0 ? 1920 : 3840, i % 3 == 0 ? 1080 : 2160, 29.97);
    }
    dir.write("CardA/readme.txt", "not footage");
    auto fs = std::make_shared<FaultyFileSystem>();
    std::string projectFile;
    ClipId failing;
    {
        auto s = ProjectSession::create(dir.path(), "Doc", fakeDependencies(extractor, fs));
        projectFile = s->model().info.projectFile;
        auto summary = ImportService::run(*s, "");
        EXPECT_EQ(summary.added, 40u);
        EXPECT_EQ(summary.skippedUnsupported, 1u);
        EXPECT_TRUE(s->pendingChanges().empty()); // import never reorganizes

        s->organize(OrganizationConfig{{MetadataCategory::Camera, MetadataCategory::Resolution}, ""});
        EXPECT_FALSE(s->pendingChanges().empty());

        // Manual refinement + tags
        FolderId john = s->edit("folders", [](ProjectModel& m) {
            auto interviews = commands::createFolder(m, m.rootFolderId(), "Interviews");
            return commands::createFolder(m, interviews, "John");
        });
        std::vector<ClipId> picked;
        for (const auto& [id, c] : s->model().clips())
            if (c.originalFilename == "C001.MOV" || c.originalFilename == "C002.MOV") picked.push_back(id);
        ASSERT_EQ(picked.size(), 2u);
        s->edit("move", [&](ProjectModel& m) { commands::moveClips(m, picked, john); });
        s->edit("tag", [&](ProjectModel& m) {
            commands::addTag(m, picked, commands::createTag(m, "Interview"));
        });
        s->applyNaming("{folder}_{number}");
        for (const auto& id : picked) {
            EXPECT_TRUE(s->model().clip(id)->placementOverride);
            EXPECT_TRUE(startsWith(s->model().clip(id)->title, "John_"));
        }

        // Re-organizing keeps the manual decisions.
        s->organize(OrganizationConfig{{MetadataCategory::Camera}, "{folder}_{number}"});
        for (const auto& id : picked) EXPECT_EQ(s->model().folderPath(s->model().clip(id)->folderId), "Interviews/John");

        // Undo/redo the last organize.
        ASSERT_TRUE(s->undo());
        ASSERT_TRUE(s->redo());

        auto pre = s->preflight();
        EXPECT_TRUE(pre.ok);

        // One operation fails at runtime.
        for (const auto& [id, c] : s->model().clips())
            if (c.originalFilename == "C007.MOV") failing = id;
        fs->failMovesInvolving = {"C007.MOV"};
        ApplyResult r = s->apply();
        EXPECT_EQ(r.state, ApplyState::PartiallyApplied);
        EXPECT_EQ(r.failed, 1u);
        EXPECT_FALSE(s->undoManager().canUndo()); // Apply is not undoable
        EXPECT_EQ(s->model().clip(failing)->filePath, "CardA/C007.MOV");
        EXPECT_TRUE(dir.exists("CardA/C007.MOV"));
        EXPECT_TRUE(dir.exists("Interviews/John/John_001.MOV"));
        EXPECT_TRUE(dir.exists("CardA/readme.txt")); // non-footage untouched; folder not removed
        EXPECT_TRUE(dir.exists("Doc.project"));
    }
    // Every file still exists exactly once: nothing lost or overwritten.
    auto files = dir.listRecursive();
    size_t movies = 0;
    for (const auto& f : files) movies += endsWith(f, ".MOV");
    EXPECT_EQ(movies, 40u);

    // Reopen: the project accurately represents the filesystem.
    auto reopened = ProjectSession::open(projectFile, fakeDependencies(extractor));
    for (const auto& [id, c] : reopened->model().clips()) {
        EXPECT_EQ(c.status, ClipStatus::Available) << c.filePath;
        EXPECT_TRUE(dir.exists(c.filePath)) << c.filePath;
    }
    auto remaining = reopened->pendingChanges();
    ASSERT_EQ(remaining.clipChanges.size(), 1u);
    EXPECT_EQ(remaining.clipChanges[0].clipId, failing);

    // A file moved away externally is shown as Missing after refresh.
    std::string some = reopened->model().clip(failing)->filePath;
    ::rename(dir.file(some).c_str(), dir.file("../moved-away.MOV").c_str());
    auto refresh = reopened->refresh();
    EXPECT_EQ(refresh.newlyMissing, std::vector<ClipId>{failing});
    ::rename(dir.file("../moved-away.MOV").c_str(), dir.file(some).c_str());
}

TEST(Workflow, ImportRejectsDirectoriesOutsideRoot) {
    TempDir dir, other;
    auto s = ProjectSession::create(dir.path(), "P", fakeDependencies());
    EXPECT_THROW(ImportService::run(*s, other.path()), UserError);
}

TEST(Workflow, IncrementalImportAddsOnlyNewFiles) {
    TempDir dir;
    dir.write("A/C001.MOV");
    auto s = ProjectSession::create(dir.path(), "P", fakeDependencies());
    EXPECT_EQ(ImportService::run(*s, "").added, 1u);
    dir.write("A/C002.MOV");
    dir.write("B/C003.MOV");
    auto r = ImportService::run(*s, dir.file("A"));
    EXPECT_EQ(r.added, 1u);
    EXPECT_EQ(r.alreadyInProject, 1u);
    EXPECT_EQ(ImportService::run(*s, "").added, 1u);
    EXPECT_EQ(s->model().clips().size(), 3u);
}

TEST(Workflow, SearchAcrossFieldsTagsAndMetadata) {
    TempDir dir;
    dir.write("Interviews/C001.MOV");
    dir.write("C002.MOV");
    auto extractor = std::make_shared<FakeExtractor>();
    extractor->byFilename["C002.MOV"] = rawFor("Sony", "ILCE-7M4", 3840, 2160, 59.94);
    auto s = ProjectSession::create(dir.path(), "P", fakeDependencies(extractor));
    ImportService::run(*s, "");
    ClipId c1;
    for (const auto& [id, c] : s->model().clips())
        if (c.originalFilename == "C001.MOV") c1 = id;
    s->edit("tag", [&](ProjectModel& m) { commands::addTag(m, {c1}, commands::createTag(m, "Good Take")); });
    EXPECT_EQ(search(s->model(), "good take").clips, std::vector<ClipId>{c1});
    EXPECT_EQ(search(s->model(), "interviews").clips, std::vector<ClipId>{c1});
    EXPECT_EQ(search(s->model(), "interviews").folders.size(), 1u);
    EXPECT_EQ(search(s->model(), "sony 59.94").clips.size(), 1u);
    EXPECT_TRUE(search(s->model(), "nothing-matches").clips.empty());
}
