#include <gtest/gtest.h>

#include "core/undo/UndoManager.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

TEST(ProjectModel, RepresentsNestedHierarchyWithOneFolderPerClip) {
    ProjectModel m;
    ModelBuilder b(m);
    auto a = b.folder("A");
    auto ab = b.folder("A/B");
    auto c1 = b.clip("A/B/C001.MOV");
    auto c2 = b.clip("C002.MOV");

    EXPECT_EQ(m.folderPath(ab), "A/B");
    EXPECT_EQ(m.clip(c1)->folderId, ab);
    EXPECT_EQ(m.clip(c2)->folderId, m.rootFolderId());
    EXPECT_EQ(m.clipsInFolder(ab), std::vector<ClipId>{c1});
    EXPECT_EQ(m.childFolders(m.rootFolderId()), std::vector<FolderId>{a});
    EXPECT_TRUE(m.isAncestorOrSelf(a, ab));
    EXPECT_FALSE(m.isAncestorOrSelf(ab, a));
    EXPECT_EQ(m.subtreeClips(a), std::vector<ClipId>{c1});
    EXPECT_EQ(m.clipIntendedPath(c1), "A/B/C001.MOV");

    // Moving a clip updates its single folder membership.
    Clip c = *m.clip(c1);
    c.folderId = a;
    m.putClip(c);
    EXPECT_TRUE(m.clipsInFolder(ab).empty());
    EXPECT_EQ(m.clipsInFolder(a), std::vector<ClipId>{c1});
}

TEST(ProjectModel, ClipIdentityIsIndependentOfHash) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("A/C001.MOV");
    auto c2 = b.clip("B/C001.MOV"); // same filename, different directory -> distinct clips
    EXPECT_NE(c1, c2);
    Clip c = *m.clip(c1);
    c.fileHash = "abc";
    c.filePath = "A/renamed.MOV";
    m.putClip(c);
    EXPECT_TRUE(m.clip(c1)); // id unchanged across hash/path changes
    EXPECT_EQ(m.clipWithFilePath("a/RENAMED.mov"), c1);
}

TEST(ProjectModel, RevertAndReapplyRestoreState) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    Clip c = *m.clip(c1);
    c.title = "Renamed";
    m.putClip(c);
    auto deltas = m.takeDeltas();
    m.revert(deltas);
    m.takeDeltas();
    EXPECT_EQ(m.clip(c1)->title, "C001");
    m.reapply(deltas);
    EXPECT_EQ(m.clip(c1)->title, "Renamed");
}

TEST(ProjectModel, UndoKeepsConfirmedPhysicalState) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    Clip c = *m.clip(c1);
    c.title = "New";
    m.putClip(c);
    auto deltas = m.takeDeltas();

    // Simulate Apply confirming a physical rename afterwards.
    Clip applied = *m.clip(c1);
    applied.filePath = "New.MOV";
    m.putClip(applied);
    m.takeDeltas();

    m.revert(deltas);
    EXPECT_EQ(m.clip(c1)->title, "C001");
    EXPECT_EQ(m.clip(c1)->filePath, "New.MOV"); // physical truth is not rolled back
}

TEST(UndoManager, UndoRedoAndClear) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    UndoManager u;
    Clip c = *m.clip(c1);
    c.title = "A";
    m.putClip(c);
    u.push("Rename", m.takeDeltas());
    EXPECT_TRUE(u.canUndo());
    EXPECT_EQ(u.undoDescription(), "Rename");
    EXPECT_TRUE(u.undo(m));
    EXPECT_EQ(m.clip(c1)->title, "C001");
    EXPECT_TRUE(u.canRedo());
    EXPECT_TRUE(u.redo(m));
    EXPECT_EQ(m.clip(c1)->title, "A");
    u.push("Nothing", {});
    EXPECT_EQ(u.depth(), 1u);
    u.clear();
    EXPECT_FALSE(u.canUndo());
}
