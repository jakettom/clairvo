#include <gtest/gtest.h>

#include "core/commands/EditCommands.h"
#include "core/organization/NamingEngine.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

TEST(NamingEngine, TemplateValidation) {
    EXPECT_FALSE(NamingEngine::validateTemplate("{folder}_{number}"));
    EXPECT_FALSE(NamingEngine::validateTemplate("{date}_{camera}_{number:4}"));
    EXPECT_TRUE(NamingEngine::validateTemplate("{unknown}"));
    EXPECT_TRUE(NamingEngine::validateTemplate("{folder"));
    EXPECT_TRUE(NamingEngine::validateTemplate("folder}"));
    EXPECT_TRUE(NamingEngine::usesNumber("{folder}_{number:4}"));
    EXPECT_FALSE(NamingEngine::usesNumber("{original}"));
}

TEST(NamingEngine, RenderVariables) {
    NamingContext ctx;
    ctx.folder = "Campus";
    ctx.number = 7;
    ctx.original = "C001";
    ctx.camera = "Sony A7IV";
    ctx.resolution = "3840x2160";
    ctx.date = "2026-10-03";
    EXPECT_EQ(NamingEngine::render("{folder}_{number}", ctx), "Campus_007");
    EXPECT_EQ(NamingEngine::render("{date}_{camera}_{resolution}_{original}_{number:2}", ctx),
              "2026-10-03_Sony A7IV_3840x2160_C001_07");
    ctx.camera.reset();
    EXPECT_EQ(NamingEngine::render("{camera}", ctx), "Unknown Camera");
    ctx.folder = "a/b";
    EXPECT_EQ(NamingEngine::render("{folder}", ctx), "a-b"); // sanitized
}

TEST(NamingEngine, SequentialDeterministicNumbering) {
    ProjectModel m;
    ModelBuilder b(m);
    auto campus = b.folder("Campus");
    auto c3 = b.clip("Campus/C003.MOV", {{MetadataCategory::Time, "2026-10-03T12:00:00Z"}});
    auto c1 = b.clip("Campus/C001.MOV", {{MetadataCategory::Time, "2026-10-03T10:00:00Z"}});
    auto c2 = b.clip("Campus/C002.MOV", {{MetadataCategory::Time, "2026-10-03T11:00:00Z"}});
    NamingEngine::applyNaming(m, "{folder}_{number}", {campus}, nullptr);
    EXPECT_EQ(m.clip(c1)->title, "Campus_001");
    EXPECT_EQ(m.clip(c2)->title, "Campus_002");
    EXPECT_EQ(m.clip(c3)->title, "Campus_003");
    EXPECT_EQ(m.clip(c1)->extension, ".MOV");
}

TEST(NamingEngine, SkipsNamesOccupiedOnDiskAndManualTitles) {
    ProjectModel m;
    ModelBuilder b(m);
    auto folder = b.folder("Interview");
    auto c1 = b.clip("Interview/C001.MOV");
    auto c2 = b.clip("Interview/C002.MOV");
    auto c3 = b.clip("Interview/C003.MOV");
    commands::renameClip(m, c3, "Interview_002"); // manual title reserves the name
    m.takeDeltas();
    // A non-project file already exists as Interview_001.MOV.
    DiskOccupancy occupied = [](const std::string& rel) { return rel == "Interview/Interview_001.MOV"; };
    NamingEngine::applyNaming(m, "{folder}_{number}", {folder}, occupied);
    EXPECT_EQ(m.clip(c1)->title, "Interview_003");
    EXPECT_EQ(m.clip(c2)->title, "Interview_004");
    EXPECT_EQ(m.clip(c3)->title, "Interview_002");
}

TEST(NamingEngine, TemplateWithoutNumberGetsSuffixes) {
    ProjectModel m;
    ModelBuilder b(m);
    auto folder = b.folder("X");
    auto c1 = b.clip("X/A.MOV", {{MetadataCategory::Camera, "Sony"}});
    auto c2 = b.clip("X/B.MOV", {{MetadataCategory::Camera, "Sony"}});
    NamingEngine::applyNaming(m, "{camera}", {folder}, nullptr);
    EXPECT_EQ(m.clip(c1)->title, "Sony");
    EXPECT_EQ(m.clip(c2)->title, "Sony_2");
}
