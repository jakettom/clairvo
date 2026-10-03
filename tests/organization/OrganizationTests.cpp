#include <gtest/gtest.h>

#include "core/changes/ChangeSet.h"
#include "core/commands/EditCommands.h"
#include "core/organization/OrganizationEngine.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

namespace {

struct SpecProject {
    ProjectModel m;
    ClipId c1, c2, c3, c4;
    SpecProject() {
        ModelBuilder b(m);
        using MC = MetadataCategory;
        c1 = b.clip("C001.MOV", {{MC::Camera, "Sony A7IV"}, {MC::Resolution, "3840x2160"}});
        c2 = b.clip("C002.MOV", {{MC::Camera, "Sony A7IV"}, {MC::Resolution, "3840x2160"}});
        c3 = b.clip("C003.MOV", {{MC::Camera, "Sony A7IV"}, {MC::Resolution, "1920x1080"}});
        c4 = b.clip("C004.MOV", {{MC::Camera, "iPhone"}, {MC::Resolution, "3840x2160"}});
    }
    std::string pathOf(const ClipId& id) { return m.clipIntendedPath(id); }
};

OrganizationConfig cameraResolution(const std::string& tmpl = "") {
    OrganizationConfig cfg;
    cfg.criteria = {MetadataCategory::Camera, MetadataCategory::Resolution};
    cfg.namingTemplate = tmpl;
    return cfg;
}

} // namespace

TEST(OrganizationEngine, SpecExampleCameraThenResolution) {
    SpecProject p;
    OrganizationEngine::organize(p.m, cameraResolution(), nullptr);
    EXPECT_EQ(p.pathOf(p.c1), "Sony A7IV/3840x2160/C001.MOV");
    EXPECT_EQ(p.pathOf(p.c2), "Sony A7IV/3840x2160/C002.MOV");
    EXPECT_EQ(p.pathOf(p.c3), "Sony A7IV/1920x1080/C003.MOV");
    EXPECT_EQ(p.pathOf(p.c4), "iPhone/3840x2160/C004.MOV");
    EXPECT_EQ(p.m.config().criteria.size(), 2u);
}

TEST(OrganizationEngine, CriterionOrderMatters) {
    SpecProject p;
    OrganizationConfig cfg;
    cfg.criteria = {MetadataCategory::Resolution, MetadataCategory::Camera};
    cfg.namingTemplate = "";
    OrganizationEngine::organize(p.m, cfg, nullptr);
    EXPECT_EQ(p.pathOf(p.c4), "3840x2160/iPhone/C004.MOV");
    EXPECT_EQ(p.pathOf(p.c3), "1920x1080/Sony A7IV/C003.MOV");
}

TEST(OrganizationEngine, MissingMetadataNeverDropsClips) {
    ProjectModel m;
    ModelBuilder b(m);
    auto known = b.clip("A.MOV", {{MetadataCategory::Camera, "Sony"}});
    auto unknown = b.clip("B.MOV");
    OrganizationConfig cfg;
    cfg.criteria = {MetadataCategory::Camera, MetadataCategory::FrameRate};
    cfg.namingTemplate = "";
    OrganizationEngine::organize(m, cfg, nullptr);
    EXPECT_EQ(m.clipIntendedPath(known), "Sony/Unknown Frame Rate/A.MOV");
    EXPECT_EQ(m.clipIntendedPath(unknown), "Unknown Camera/Unknown Frame Rate/B.MOV");
    EXPECT_EQ(m.clips().size(), 2u);
}

TEST(OrganizationEngine, DeterministicAcrossRuns) {
    SpecProject a, b;
    OrganizationEngine::organize(a.m, cameraResolution("{folder}_{number}"), nullptr);
    OrganizationEngine::organize(b.m, cameraResolution("{folder}_{number}"), nullptr);
    auto collect = [](SpecProject& p) {
        std::vector<std::string> out;
        for (const auto& id : {p.c1, p.c2, p.c3, p.c4}) out.push_back(p.pathOf(id));
        return out;
    };
    EXPECT_EQ(collect(a), collect(b));
    EXPECT_EQ(a.pathOf(a.c1), "Sony A7IV/3840x2160/3840x2160_001.MOV");
    EXPECT_EQ(a.pathOf(a.c2), "Sony A7IV/3840x2160/3840x2160_002.MOV");
}

TEST(OrganizationEngine, ManualOverridesSurviveReorganizationUntilReset) {
    SpecProject p;
    OrganizationEngine::organize(p.m, cameraResolution("{folder}_{number}"), nullptr);
    auto interviews = commands::createFolder(p.m, p.m.rootFolderId(), "Interviews");
    auto john = commands::createFolder(p.m, interviews, "John");
    commands::moveClips(p.m, {p.c1}, john);
    p.m.takeDeltas();
    EXPECT_TRUE(p.m.clip(p.c1)->placementOverride);

    // Re-organize with different criteria: the manual placement wins, but the
    // naming template still applies (spec §70 step 7).
    OrganizationConfig cfg;
    cfg.criteria = {MetadataCategory::Camera};
    cfg.namingTemplate = "{folder}_{number}";
    OrganizationEngine::organize(p.m, cfg, nullptr);
    EXPECT_EQ(p.pathOf(p.c1), "Interviews/John/John_001.MOV");
    EXPECT_EQ(p.pathOf(p.c2), "Sony A7IV/Sony A7IV_001.MOV");
    // Emptied automatic folders are pruned; manual folders are kept.
    for (const auto& [id, f] : p.m.folders()) EXPECT_NE(f.name, "1920x1080");
    EXPECT_TRUE(p.m.folder(interviews));

    OrganizationEngine::resetToAutomatic(p.m, {p.c1}, nullptr);
    EXPECT_FALSE(p.m.clip(p.c1)->placementOverride);
    EXPECT_EQ(p.m.folderPath(p.m.clip(p.c1)->folderId), "Sony A7IV");
}

TEST(OrganizationEngine, ResetOrganizationReturnsToPhysicalLayout) {
    ProjectModel m;
    ModelBuilder b(m);
    auto camA = b.folder("CameraA");
    auto c1 = b.clip("CameraA/C001.MOV", {{MetadataCategory::Camera, "Sony"}});
    auto c2 = b.clip("C002.MOV", {{MetadataCategory::Camera, "iPhone"}});
    OrganizationEngine::organize(m, OrganizationConfig{{MetadataCategory::Camera}, "{folder}_{number}"}, nullptr);
    commands::renameClip(m, c2, "Custom");
    m.takeDeltas();
    EXPECT_FALSE(computeChanges(m).empty());

    OrganizationEngine::resetOrganization(m);
    EXPECT_TRUE(computeChanges(m).empty());
    EXPECT_EQ(m.clipIntendedPath(c1), "CameraA/C001.MOV");
    EXPECT_EQ(m.clipIntendedPath(c2), "C002.MOV");
    EXPECT_FALSE(m.clip(c2)->titleOverride);
    // The imported folder was pruned by organize and is recreated at its physical place.
    (void)camA;
    EXPECT_TRUE(m.folderWithPhysicalPath("CameraA"));
}

TEST(OrganizationEngine, EmptyCriteriaPlacesClipsByPhysicalDirectory) {
    ProjectModel m;
    ModelBuilder b(m);
    auto c1 = b.clip("A/C001.MOV");
    auto other = commands::createFolder(m, m.rootFolderId(), "Other");
    Clip c = *m.clip(c1);
    c.folderId = other; // moved automatically (no override)
    m.putClip(c);
    m.takeDeltas();
    OrganizationEngine::organize(m, OrganizationConfig{{}, ""}, nullptr);
    EXPECT_EQ(m.clipIntendedPath(c1), "A/C001.MOV");
}
