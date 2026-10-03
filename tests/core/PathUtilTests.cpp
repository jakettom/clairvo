#include <gtest/gtest.h>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

using namespace vo;

TEST(PathUtil, ComponentsAndJoin) {
    EXPECT_EQ(path::join("", "a.mov"), "a.mov");
    EXPECT_EQ(path::join("A/B", "c.mov"), "A/B/c.mov");
    EXPECT_EQ(path::parent("A/B/c.mov"), "A/B");
    EXPECT_EQ(path::parent("c.mov"), "");
    EXPECT_EQ(path::filename("A/B/c.mov"), "c.mov");
    EXPECT_EQ(path::stem("C001.MOV"), "C001");
    EXPECT_EQ(path::extension("C001.MOV"), ".MOV");
    EXPECT_EQ(path::extension(".hidden"), "");
}

TEST(PathUtil, RebaseOnlyOnComponentBoundaries) {
    EXPECT_EQ(path::rebase("A/B/c.mov", "A", "X"), "X/B/c.mov");
    EXPECT_EQ(path::rebase("AB/c.mov", "A", "X"), "AB/c.mov");
    EXPECT_EQ(path::rebase("A", "A", "X/Y"), "X/Y");
    EXPECT_TRUE(path::isWithin("A/B", "A"));
    EXPECT_FALSE(path::isWithin("AB", "A"));
}

TEST(PathUtil, RelativeTo) {
    std::string rel;
    EXPECT_TRUE(path::relativeTo("/Footage", "/Footage/A/b.mov", rel));
    EXPECT_EQ(rel, "A/b.mov");
    EXPECT_TRUE(path::relativeTo("/Footage/", "/Footage", rel));
    EXPECT_EQ(rel, "");
    EXPECT_FALSE(path::relativeTo("/Footage", "/FootageOther/x", rel));
}

TEST(PathUtil, NameValidationAndSanitizing) {
    EXPECT_TRUE(path::isValidName("John_001.MOV"));
    EXPECT_FALSE(path::isValidName(""));
    EXPECT_FALSE(path::isValidName("a/b"));
    EXPECT_FALSE(path::isValidName("a:b"));
    EXPECT_FALSE(path::isValidName(".hidden"));
    EXPECT_FALSE(path::isValidName(" padded"));
    EXPECT_EQ(path::sanitizeName("Sony/A7:IV"), "Sony-A7-IV");
    EXPECT_EQ(path::sanitizeName("..."), "Untitled");
    EXPECT_EQ(path::sanitizeName("  .x  "), "x");
}

TEST(StringUtil, NaturalOrdering) {
    EXPECT_TRUE(naturalLess("C2", "C10"));
    EXPECT_FALSE(naturalLess("C10", "C2"));
    EXPECT_TRUE(naturalLess("a", "B"));
    EXPECT_TRUE(equalsIgnoreCase("Camera", "cAMERA"));
}
