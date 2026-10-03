#include <gtest/gtest.h>

#include <sys/stat.h>

#include "core/filesystem/FileSystem.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

TEST(PosixFileSystem, CreateMoveRename) {
    TempDir t;
    PosixFileSystem fs;
    EXPECT_FALSE(fs.createDirectory(t.file("A")));
    EXPECT_TRUE(fs.isDirectory(t.file("A")));
    auto again = fs.createDirectory(t.file("A"));
    ASSERT_TRUE(again);
    EXPECT_EQ(again->kind, FsErrorKind::AlreadyExists);

    t.write("C001.MOV", "one");
    EXPECT_FALSE(fs.moveNoReplace(t.file("C001.MOV"), t.file("A/C001.MOV")));
    EXPECT_TRUE(t.exists("A/C001.MOV"));
    EXPECT_FALSE(fs.renameFile(t.file("A/C001.MOV"), "John_001.MOV"));
    EXPECT_EQ(t.read("A/John_001.MOV"), "one");
    auto bad = fs.renameFile(t.file("A/John_001.MOV"), "bad/name");
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->kind, FsErrorKind::InvalidName);
}

TEST(PosixFileSystem, NeverOverwrites) {
    TempDir t;
    PosixFileSystem fs;
    t.write("a.mov", "A");
    t.write("b.mov", "B");
    auto err = fs.moveNoReplace(t.file("a.mov"), t.file("b.mov"));
    ASSERT_TRUE(err);
    EXPECT_EQ(err->kind, FsErrorKind::AlreadyExists);
    EXPECT_EQ(t.read("a.mov"), "A");
    EXPECT_EQ(t.read("b.mov"), "B");
}

TEST(PosixFileSystem, DistinguishesMissingSourceFromPermissionFailure) {
    TempDir t;
    PosixFileSystem fs;
    auto missing = fs.moveNoReplace(t.file("nope.mov"), t.file("x.mov"));
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->kind, FsErrorKind::NotFound);
    EXPECT_EQ(missing->message, "Source not found");

    t.write("locked/c.mov");
    t.mkdir("dest");
    ::chmod(t.file("locked").c_str(), 0555);
    auto denied = fs.moveNoReplace(t.file("locked/c.mov"), t.file("dest/c.mov"));
    ::chmod(t.file("locked").c_str(), 0755);
    ASSERT_TRUE(denied);
    EXPECT_EQ(denied->kind, FsErrorKind::PermissionDenied);

    auto noDir = fs.moveNoReplace(t.file("locked/c.mov"), t.file("missing/c.mov"));
    ASSERT_TRUE(noDir);
    EXPECT_EQ(noDir->kind, FsErrorKind::NotFound);
}

TEST(PosixFileSystem, RemoveEmptyDirectoryNeverRemovesContent) {
    TempDir t;
    PosixFileSystem fs;
    t.write("full/clip.mov");
    EXPECT_TRUE(fs.removeEmptyDirectory(t.file("full")));
    EXPECT_TRUE(t.exists("full/clip.mov"));
    t.write("finder/.DS_Store");
    EXPECT_FALSE(fs.removeEmptyDirectory(t.file("finder")));
    EXPECT_FALSE(t.exists("finder"));
}
