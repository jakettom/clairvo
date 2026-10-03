#include <gtest/gtest.h>

#include <sys/stat.h>

#include "core/changes/ChangeSet.h"
#include "core/commands/EditCommands.h"
#include "core/filesystem/ApplyEngine.h"
#include "core/filesystem/MissingFileDetector.h"
#include "core/filesystem/PreflightValidator.h"
#include "core/organization/OrganizationEngine.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

namespace {

struct Fixture {
    TempDir dir;
    ProjectModel m;
    std::unique_ptr<ModelBuilder> b;
    Fixture() {
        b = std::make_unique<ModelBuilder>(m, dir.path(), "Doc");
        dir.write("Doc.project", "db");
    }
    ClipId clip(const std::string& rel, std::map<MetadataCategory, std::string> md = {}) {
        dir.write(rel, rel);
        return b->clip(rel, std::move(md));
    }
};

} // namespace

TEST(Apply, VerticalSliceOrganizeByCamera) {
    Fixture f;
    auto c1 = f.clip("C001.MOV", {{MetadataCategory::Camera, "Sony A7IV"}});
    OrganizationEngine::organize(f.m, OrganizationConfig{{MetadataCategory::Camera}, ""}, nullptr);
    f.m.takeDeltas();

    PosixFileSystem fs;
    PreflightReport pre = runPreflight(f.m, computeChanges(f.m), fs);
    EXPECT_TRUE(pre.ok);
    EXPECT_EQ(pre.folderCreates, 1u);
    EXPECT_EQ(pre.clipMoves, 1u);

    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    EXPECT_EQ(r.state, ApplyState::Applied);
    EXPECT_EQ(r.failed, 0u);
    EXPECT_EQ(f.dir.read("Sony A7IV/C001.MOV"), "C001.MOV");
    EXPECT_TRUE(f.dir.exists("Doc.project")); // project file stays at the root
    EXPECT_EQ(f.m.clip(c1)->filePath, "Sony A7IV/C001.MOV");
    EXPECT_TRUE(computeChanges(f.m).empty());
    for (const auto& e : journal.entries) EXPECT_EQ(e.status, "SUCCESS");
}

TEST(Apply, FullSpecWorkflowWithNamingAndManualOverride) {
    Fixture f;
    using MC = MetadataCategory;
    auto c1 = f.clip("C001.MOV", {{MC::Camera, "Sony A7IV"}, {MC::Resolution, "3840x2160"}});
    auto c2 = f.clip("C002.MOV", {{MC::Camera, "Sony A7IV"}, {MC::Resolution, "3840x2160"}});
    auto c3 = f.clip("C003.MOV", {{MC::Camera, "iPhone"}, {MC::Resolution, "3840x2160"}});
    auto c4 = f.clip("C004.MOV", {{MC::Camera, "iPhone"}, {MC::Resolution, "1920x1080"}});
    OrganizationEngine::organize(f.m, OrganizationConfig{{MC::Camera, MC::Resolution}, ""}, nullptr);
    auto interviews = commands::createFolder(f.m, f.m.rootFolderId(), "Interviews");
    auto john = commands::createFolder(f.m, interviews, "John");
    commands::moveClips(f.m, {c1}, john);
    OrganizationEngine::applyNaming(f.m, "{folder}_{number}", nullptr);
    f.m.takeDeltas();

    PosixFileSystem fs;
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    ASSERT_EQ(r.state, ApplyState::Applied);
    EXPECT_EQ(f.dir.listRecursive(), (std::vector<std::string>{
                                         "Doc.project",
                                         "Interviews/John/John_001.MOV",
                                         "Sony A7IV/3840x2160/3840x2160_001.MOV",
                                         "iPhone/1920x1080/1920x1080_001.MOV",
                                         "iPhone/3840x2160/3840x2160_001.MOV",
                                     }));
    EXPECT_EQ(f.dir.read("Interviews/John/John_001.MOV"), "C001.MOV");
    EXPECT_EQ(f.m.clip(c2)->filePath, "Sony A7IV/3840x2160/3840x2160_001.MOV");
    (void)c3;
    (void)c4;
}

TEST(Apply, PartialFailureIsReportedAndDatabaseReflectsReality) {
    Fixture f;
    auto ok = f.clip("A.MOV", {{MetadataCategory::Camera, "Sony"}});
    auto bad = f.clip("B.MOV", {{MetadataCategory::Camera, "Sony"}});
    OrganizationEngine::organize(f.m, OrganizationConfig{{MetadataCategory::Camera}, ""}, nullptr);
    f.m.takeDeltas();

    FaultyFileSystem fs;
    fs.failMovesInvolving = {"B.MOV"};
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    EXPECT_EQ(r.state, ApplyState::PartiallyApplied);
    EXPECT_EQ(r.failed, 1u);
    EXPECT_EQ(r.succeeded, 2u); // folder + A.MOV
    EXPECT_EQ(f.m.clip(ok)->filePath, "Sony/A.MOV");
    EXPECT_EQ(f.m.clip(bad)->filePath, "B.MOV"); // not claimed as moved
    EXPECT_TRUE(f.dir.exists("B.MOV"));
    bool reported = false;
    for (const auto& op : r.operations)
        if (!op.success) reported = op.error == "Permission denied" && op.clipId == bad;
    EXPECT_TRUE(reported);
    // The failed operation remains identifiable as a pending change.
    ChangeSet remaining = computeChanges(f.m);
    ASSERT_EQ(remaining.clipChanges.size(), 1u);
    EXPECT_EQ(remaining.clipChanges[0].clipId, bad);
}

TEST(Apply, RealPermissionFailure) {
    Fixture f;
    auto c = f.clip("locked/C001.MOV");
    auto dest = commands::createFolder(f.m, f.m.rootFolderId(), "Dest");
    commands::moveClips(f.m, {c}, dest);
    f.m.takeDeltas();
    ::chmod(f.dir.file("locked").c_str(), 0555);
    PosixFileSystem fs;
    PreflightReport pre = runPreflight(f.m, computeChanges(f.m), fs);
    EXPECT_FALSE(pre.ok); // preflight catches the unwritable source folder
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    ::chmod(f.dir.file("locked").c_str(), 0755);
    EXPECT_EQ(r.state, ApplyState::PartiallyApplied);
    EXPECT_EQ(f.m.clip(c)->filePath, "locked/C001.MOV");
}

TEST(Apply, PreflightBlocksOccupiedDestinationAndNeverOverwrites) {
    Fixture f;
    auto c = f.clip("C001.MOV");
    f.dir.write("Intruder.MOV", "not ours");
    commands::renameClip(f.m, c, "Intruder");
    f.m.takeDeltas();
    PosixFileSystem fs;
    PreflightReport pre = runPreflight(f.m, computeChanges(f.m), fs);
    EXPECT_FALSE(pre.ok);
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    EXPECT_EQ(r.state, ApplyState::Failed);
    EXPECT_EQ(f.dir.read("Intruder.MOV"), "not ours");
    EXPECT_EQ(f.dir.read("C001.MOV"), "C001.MOV");
}

TEST(Apply, SwapCycleAndCaseOnlyRename) {
    Fixture f;
    auto a = f.clip("A.MOV");
    auto b = f.clip("B.MOV");
    auto c = f.clip("lower.MOV");
    commands::renameClip(f.m, a, "tmp");
    commands::renameClip(f.m, b, "A");
    commands::renameClip(f.m, a, "B");
    commands::renameClip(f.m, c, "LOWER");
    f.m.takeDeltas();
    PosixFileSystem fs;
    EXPECT_TRUE(runPreflight(f.m, computeChanges(f.m), fs).ok);
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    EXPECT_EQ(r.state, ApplyState::Applied);
    EXPECT_EQ(f.dir.read("A.MOV"), "B.MOV");
    EXPECT_EQ(f.dir.read("B.MOV"), "A.MOV");
    auto files = f.dir.listRecursive();
    EXPECT_NE(std::find(files.begin(), files.end(), "LOWER.MOV"), files.end());
    EXPECT_TRUE(computeChanges(f.m).empty());
}

TEST(Apply, FolderRenameMovesDirectoryAndRemovesEmptiedFolders) {
    Fixture f;
    auto camA = f.b->folder("CameraA");
    auto c1 = f.clip("CameraA/Day1/C001.MOV");
    auto c2 = f.clip("Old/C002.MOV");
    commands::renameFolder(f.m, camA, "Sony");
    commands::moveClips(f.m, {c2}, f.m.rootFolderId());
    auto old = f.m.folderWithPhysicalPath("Old");
    commands::deleteFolder(f.m, *old, commands::DeleteFolderBehavior::MoveClipsToParent);
    f.m.takeDeltas();
    PosixFileSystem fs;
    MemoryJournal journal;
    ApplyResult r = ApplyEngine(f.m, fs, journal).execute();
    EXPECT_EQ(r.state, ApplyState::Applied);
    EXPECT_TRUE(f.dir.exists("Sony/Day1/C001.MOV"));
    EXPECT_EQ(f.m.clip(c1)->filePath, "Sony/Day1/C001.MOV");
    EXPECT_EQ(*f.m.folder(camA)->physicalPath, "Sony");
    EXPECT_FALSE(f.dir.exists("Old")); // emptied, unrepresented directory removed
    EXPECT_EQ(r.removedDirectories, std::vector<std::string>{"Old"});
}

TEST(Apply, CrashRecoveryReconcilesPendingOperations) {
    Fixture f;
    auto c1 = f.clip("C001.MOV");
    auto c2 = f.clip("C002.MOV");
    f.dir.mkdir("Done");
    // Simulate: C001 was moved on disk but the process died before the result
    // was recorded; C002's move never happened.
    ::rename(f.dir.file("C001.MOV").c_str(), f.dir.file("Done/C001.MOV").c_str());
    std::vector<PendingOperation> pending = {
        {1, "b", "MOVE_CLIP", c1, "", "C001.MOV", "Done/C001.MOV"},
        {2, "b", "MOVE_CLIP", c2, "", "C002.MOV", "Done/C002.MOV"},
    };
    PosixFileSystem fs;
    MemoryJournal journal;
    journal.entries.resize(3);
    auto report = ApplyEngine::recover(f.m, fs, journal, pending);
    ASSERT_EQ(report.size(), 2u);
    EXPECT_TRUE(report[0].success);
    EXPECT_FALSE(report[1].success);
    EXPECT_EQ(f.m.clip(c1)->filePath, "Done/C001.MOV");
    EXPECT_EQ(f.m.clip(c2)->filePath, "C002.MOV");
}

TEST(MissingFiles, DetectedNotRemovedAndRecovered) {
    Fixture f;
    auto c = f.clip("A/C001.MOV");
    PosixFileSystem fs;
    ::rename(f.dir.file("A/C001.MOV").c_str(), f.dir.file("elsewhere.MOV").c_str());
    auto r = refreshPhysicalState(f.m, fs);
    EXPECT_EQ(r.newlyMissing, std::vector<ClipId>{c});
    ASSERT_TRUE(f.m.clip(c)); // still represented
    EXPECT_EQ(f.m.clip(c)->status, ClipStatus::Missing);
    ::rename(f.dir.file("elsewhere.MOV").c_str(), f.dir.file("A/C001.MOV").c_str());
    r = refreshPhysicalState(f.m, fs);
    EXPECT_EQ(r.recovered, std::vector<ClipId>{c});
    EXPECT_EQ(f.m.clip(c)->status, ClipStatus::Available);
}
