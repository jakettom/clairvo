#include <gtest/gtest.h>

#include <chrono>

#include "core/changes/ChangeSet.h"
#include "core/organization/OrganizationEngine.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

// Synthetic large project (roadmap §28): organization and change derivation
// must stay interactive for 10,000 clips.
TEST(Stress, TenThousandClipsOrganizeAndDiff) {
    ProjectModel m;
    ModelBuilder b(m);
    const char* cameras[] = {"Sony A7IV", "iPhone 15 Pro", "Canon R5", "DJI Mini 4"};
    const char* resolutions[] = {"3840x2160", "1920x1080", "5472x3078"};
    for (int i = 0; i < 10000; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "Card%02d/C%05d.MOV", i % 25, i);
        b.clip(name, {{MetadataCategory::Camera, cameras[i % 4]},
                      {MetadataCategory::Resolution, resolutions[i % 3]},
                      {MetadataCategory::FrameRate, i % 2 ? "29.97" : "59.94"}});
    }
    auto start = std::chrono::steady_clock::now();
    OrganizationEngine::organize(
        m, OrganizationConfig{{MetadataCategory::Camera, MetadataCategory::Resolution, MetadataCategory::FrameRate},
                              "{folder}_{number}"},
        nullptr);
    auto deltas = m.takeDeltas();
    ChangeSet cs = computeChanges(m);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    EXPECT_EQ(cs.clipChanges.size(), 10000u);
    EXPECT_EQ(m.clips().size(), 10000u);
    EXPECT_LT(elapsed.count(), 15000) << "organize + diff took " << elapsed.count() << " ms";
    m.revert(deltas);
    m.takeDeltas();
    EXPECT_TRUE(computeChanges(m).empty());
}
