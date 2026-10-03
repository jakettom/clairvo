#include <gtest/gtest.h>

#include "core/commands/EditCommands.h"
#include "core/undo/UndoManager.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

namespace {

// Runs a command, then verifies that undo restores the original state and
// redo restores the edited state.
template <class F>
void expectUndoable(ProjectModel& m, F&& command) {
    auto snapshot = [&] {
        std::vector<std::string> out;
        for (const auto& [id, f] : m.folders()) out.push_back("F" + id + f.parentId + f.name);
        for (const auto& [id, c] : m.clips()) {
            std::string tags;
            for (const auto& t : m.clipTags(id)) tags += t;
            out.push_back("C" + id + c.folderId + c.title + (c.placementOverride ? "P" : "") + tags);
        }
        for (const auto& [id, t] : m.tags()) out.push_back("T" + id + t.name);
        std::sort(out.begin(), out.end());
        return out;
    };
    auto before = snapshot();
    UndoManager u;
    command();
    auto after = snapshot();
    u.push("cmd", m.takeDeltas());
    ASSERT_TRUE(u.undo(m));
    m.takeDeltas();
    EXPECT_EQ(snapshot(), before);
    ASSERT_TRUE(u.redo(m));
    m.takeDeltas();
    EXPECT_EQ(snapshot(), after);
}

} // namespace

TEST(Commands, CreateRenameMoveDeleteFolder) {
    ProjectModel m;
    ModelBuilder b(m);
    FolderId interviews;
    expectUndoable(m, [&] { interviews = commands::createFolder(m, m.rootFolderId(), "Interviews"); });
    EXPECT_EQ(m.folder(interviews)->name, "Interviews");
    EXPECT_EQ(m.folder(interviews)->origin, FolderOrigin::Manual);

    // Duplicate names are uniquified on create, rejected on rename.
    auto second = commands::createFolder(m, m.rootFolderId(), "interviews");
    EXPECT_EQ(m.folder(second)->name, "interviews 2");
    EXPECT_THROW(commands::renameFolder(m, second, "INTERVIEWS"), UserError);
    m.takeDeltas();

    expectUndoable(m, [&] { commands::renameFolder(m, second, "B-Roll"); });
    expectUndoable(m, [&] { commands::moveFolders(m, {second}, interviews); });
    EXPECT_EQ(m.folderPath(second), "Interviews/B-Roll");
    EXPECT_THROW(commands::moveFolders(m, {interviews}, second), UserError); // into own subtree
    EXPECT_THROW(commands::renameFolder(m, m.rootFolderId(), "X"), UserError);
    EXPECT_THROW(commands::createFolder(m, m.rootFolderId(), "a/b"), UserError);
    m.takeDeltas();

    auto clip = b.clip("C001.MOV");
    commands::moveClips(m, {clip}, second);
    m.takeDeltas();
    expectUndoable(m, [&] {
        commands::deleteFolder(m, interviews, commands::DeleteFolderBehavior::MoveClipsToParent);
    });
    EXPECT_FALSE(m.folder(interviews));
    EXPECT_FALSE(m.folder(second));
    EXPECT_EQ(m.clip(clip)->folderId, m.rootFolderId()); // preserved in project
}

TEST(Commands, DeleteFolderCanRemoveClipsFromProjectOnly) {
    ProjectModel m;
    ModelBuilder b(m);
    auto folder = b.folder("Interview");
    auto c1 = b.clip("Interview/C001.MOV");
    auto c2 = b.clip("Interview/C002.MOV");
    expectUndoable(m, [&] { commands::deleteFolder(m, folder, commands::DeleteFolderBehavior::RemoveClipsFromProject); });
    EXPECT_FALSE(m.clip(c1));
    EXPECT_FALSE(m.clip(c2));
}

TEST(Commands, MoveAndRenameClipsSetOverrides) {
    ProjectModel m;
    ModelBuilder b(m);
    auto dest = b.folder("Dest", FolderOrigin::Manual);
    auto c1 = b.clip("A/C001.MOV");
    auto c2 = b.clip("B/C001.MOV");
    expectUndoable(m, [&] { commands::moveClips(m, {c1, c2}, dest); });
    EXPECT_TRUE(m.clip(c1)->placementOverride);
    // Same-named clips moved into one folder are made unique.
    EXPECT_NE(m.clip(c1)->title, m.clip(c2)->title);

    expectUndoable(m, [&] { commands::renameClip(m, c1, "John_001.mov"); });
    EXPECT_EQ(m.clip(c1)->title, "John_001"); // typed extension stripped
    EXPECT_EQ(m.clip(c1)->extension, ".MOV"); // original extension preserved
    EXPECT_TRUE(m.clip(c1)->titleOverride);
    EXPECT_THROW(commands::renameClip(m, c1, "bad/name"), UserError);
    EXPECT_THROW(commands::renameClip(m, c1, "   "), UserError);
}

TEST(Commands, Tags) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV");
    auto c2 = b.clip("C002.MOV");
    TagId interview;
    expectUndoable(m, [&] { interview = commands::createTag(m, "Interview"); });
    EXPECT_EQ(commands::createTag(m, "interview"), interview); // controlled vocabulary
    expectUndoable(m, [&] { commands::addTag(m, {c1, c2}, interview); });
    EXPECT_TRUE(m.clipTags(c1).count(interview));
    expectUndoable(m, [&] { commands::removeTag(m, {c2}, interview); });
    EXPECT_FALSE(m.clipTags(c2).count(interview));
    expectUndoable(m, [&] { commands::renameTag(m, interview, "Interviews"); });
    expectUndoable(m, [&] { commands::deleteTag(m, interview); });
    EXPECT_FALSE(m.tag(interview));
    EXPECT_TRUE(m.clipTags(c1).empty());
}

TEST(Commands, RemoveClipsIsUndoableIncludingMetadataAndTags) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("C001.MOV", {{MetadataCategory::Camera, "Sony"}});
    auto tag = commands::createTag(m, "Good Take");
    commands::addTag(m, {c1}, tag);
    UndoManager u;
    u.push("tag", m.takeDeltas());
    commands::removeClips(m, {c1});
    u.push("remove", m.takeDeltas());
    EXPECT_FALSE(m.clip(c1));
    u.undo(m);
    ASSERT_TRUE(m.clip(c1));
    EXPECT_EQ(m.normalizedValue(c1, MetadataCategory::Camera), "Sony");
    EXPECT_TRUE(m.clipTags(c1).count(tag));
}
