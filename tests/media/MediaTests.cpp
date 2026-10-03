#include <gtest/gtest.h>

#include "core/metadata/MetadataNormalizer.h"
#include "media/extraction/MetadataExtractor.h"
#include "media/formats/SupportedFormats.h"
#include "media/hashing/FileHasher.h"
#include "media/scanning/DirectoryScanner.h"
#include "support/TestSupport.h"

using namespace vo;
using namespace vo::test;

TEST(SupportedFormats, VideoExtensions) {
    EXPECT_TRUE(media::isSupportedVideoFile("C001.MOV"));
    EXPECT_TRUE(media::isSupportedVideoFile("clip.mp4"));
    EXPECT_TRUE(media::isSupportedVideoFile("clip.M4V"));
    EXPECT_FALSE(media::isSupportedVideoFile("photo.jpg"));
    EXPECT_FALSE(media::isSupportedVideoFile("._C001.MOV")); // AppleDouble
    EXPECT_FALSE(media::isSupportedVideoFile("Doc.project"));
}

TEST(DirectoryScanner, RecursiveFilteredAndSorted) {
    TempDir t;
    t.write("CameraA/C001.MOV");
    t.write("CameraA/C010.MOV");
    t.write("CameraA/C002.MOV");
    t.write("CameraB/sub/clip.mp4");
    t.write("notes.txt");
    t.write(".hidden/secret.mov");
    t.write("Library.fcpbundle/media.mov");
    t.write("Doc.project");
    auto r = media::scanDirectory(t.path(), "");
    std::vector<std::string> paths;
    for (const auto& f : r.files) paths.push_back(f.relativePath);
    EXPECT_EQ(paths, (std::vector<std::string>{"CameraA/C001.MOV", "CameraA/C002.MOV", "CameraA/C010.MOV",
                                               "CameraB/sub/clip.mp4"}));
    EXPECT_EQ(r.skippedUnsupported, 1u);
    auto sub = media::scanDirectory(t.path(), "CameraB");
    ASSERT_EQ(sub.files.size(), 1u);
    EXPECT_EQ(sub.files[0].relativePath, "CameraB/sub/clip.mp4");
}

TEST(FileHasher, FingerprintIsStableAndContentSensitive) {
    TempDir t;
    t.write("a.mov", "hello");
    t.write("b.mov", "hello");
    t.write("c.mov", "world");
    auto a = media::fingerprintFile(t.file("a.mov"));
    ASSERT_TRUE(a);
    EXPECT_EQ(a->size(), 64u);
    EXPECT_EQ(a, media::fingerprintFile(t.file("b.mov")));
    EXPECT_NE(a, media::fingerprintFile(t.file("c.mov")));
    EXPECT_FALSE(media::fingerprintFile(t.file("missing.mov")));
}

TEST(AVFoundationExtractor, ExtractsAndNormalizesRealMovie) {
    TempDir t;
    std::string file = t.file("C001.MOV");
    ASSERT_TRUE(writeTestMovie(file, 640, 360, "Apple", "iPhone 15 Pro", "+42.3736-071.1097+012.000/",
                               "2026-10-03T14:22:05-0400"));
    auto extractor = media::makeAVFoundationExtractor();
    auto raw = extractor->extract(file);
    ASSERT_FALSE(raw.empty());
    auto n = MetadataNormalizer::normalize(raw);
    EXPECT_EQ(n[MetadataCategory::Resolution], "640x360");
    EXPECT_EQ(n[MetadataCategory::Codec], "H.264");
    EXPECT_EQ(n[MetadataCategory::Orientation], "Landscape");
    EXPECT_EQ(n[MetadataCategory::Camera], "Apple iPhone 15 Pro");
    EXPECT_EQ(n[MetadataCategory::Location], "42.373600, -71.109700");
    EXPECT_EQ(n[MetadataCategory::Time].substr(0, 10), "2026-10-03");
    EXPECT_TRUE(n.count(MetadataCategory::Duration));
    EXPECT_TRUE(n.count(MetadataCategory::FrameRate));
}

TEST(AVFoundationExtractor, NonVideoFileYieldsNoMetadataWithoutCrashing) {
    TempDir t;
    t.write("broken.mov", "this is not a movie");
    auto raw = media::makeAVFoundationExtractor()->extract(t.file("broken.mov"));
    auto n = MetadataNormalizer::normalize(raw);
    EXPECT_FALSE(n.count(MetadataCategory::Resolution));
}
