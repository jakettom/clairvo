#include <gtest/gtest.h>

#include "core/changes/ChangeSet.h"
#include "core/commands/EditCommands.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

TEST(ChangeSet, NoChangesForFreshImport) {
    ProjectModel m;
    ModelBuilder b(m);
    b.clip("A/C001.MOV");
    b.clip("C002.MOV");
    EXPECT_TRUE(computeChanges(m).empty());
}

TEST(ChangeSet, CreateMoveRenameDerivedFromVirtualState) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    auto interviews = commands::createFolder(m, m.rootFolderId(), "Interviews");
    auto john = commands::createFolder(m, interviews, "John");
    commands::moveClips(m, {c1}, john);
    commands::renameClip(m, c1, "John_001");
    m.takeDeltas();

    ChangeSet cs = computeChanges(m);
    ASSERT_EQ(cs.folderChanges.size(), 2u);
    EXPECT_EQ(cs.folderChanges[0].type, ChangeType::CreateFolder);
    EXPECT_EQ(cs.folderChanges[0].destinationPath, "Interviews"); // parents first
    EXPECT_EQ(cs.folderChanges[1].destinationPath, "Interviews/John");
    ASSERT_EQ(cs.clipChanges.size(), 1u);
    const Change& c = cs.clipChanges[0];
    EXPECT_EQ(c.type, ChangeType::MoveClip);
    EXPECT_EQ(c.sourcePath, "C001.MOV");
    EXPECT_EQ(c.destinationPath, "Interviews/John/John_001.MOV");
    EXPECT_TRUE(c.moves);
    EXPECT_TRUE(c.renames);
    EXPECT_EQ(c.origin, ChangeOrigin::Manual);
}

TEST(ChangeSet, FolderRenameCarriesContents) {
    ProjectModel m;
    ModelBuilder b(m);
    auto camA = b.folder("CameraA");
    b.folder("CameraA/Day1");
    auto c1 = b.clip("CameraA/Day1/C001.MOV");
    commands::renameFolder(m, camA, "Sony");
    m.takeDeltas();
    ChangeSet cs = computeChanges(m);
    ASSERT_EQ(cs.folderChanges.size(), 1u);
    EXPECT_EQ(cs.folderChanges[0].type, ChangeType::RenameFolder);
    EXPECT_EQ(cs.folderChanges[0].sourcePath, "CameraA");
    EXPECT_EQ(cs.folderChanges[0].destinationPath, "Sony");
    // The clip moves with its directory; no separate clip operation.
    EXPECT_TRUE(cs.clipChanges.empty());
    (void)c1;
}

TEST(ChangeSet, MissingClipsAreSkipped) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    Clip c = *m.clip(c1);
    c.status = ClipStatus::Missing;
    c.title = "Renamed";
    m.putClip(c);
    ChangeSet cs = computeChanges(m);
    EXPECT_TRUE(cs.clipChanges.empty());
    EXPECT_EQ(cs.skippedMissing, std::vector<ClipId>{c1});
}
