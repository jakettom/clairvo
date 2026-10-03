#include <gtest/gtest.h>

#include "core/metadata/MetadataNormalizer.h"

using namespace vo;

TEST(MetadataNormalizer, IPhoneQuickTimeKeys) {
    std::vector<RawMetadataEntry> raw = {
        {"mdta/com.apple.quicktime.make", "Apple", "container"},
        {"mdta/com.apple.quicktime.model", "iPhone 15 Pro", "container"},
        {"mdta/com.apple.quicktime.location.ISO6709", "+42.3736-071.1097+012.000/", "container"},
        {"mdta/com.apple.quicktime.creationdate", "2026-10-03T14:22:05-0400", "container"},
        {"mdta/com.apple.quicktime.camera.lens_model", "iPhone 15 Pro back camera 6.86mm f/1.78", "container"},
        {"track.video.width", "3840", "avfoundation"},
        {"track.video.height", "2160", "avfoundation"},
        {"track.video.rotation", "90", "avfoundation"},
        {"track.video.frameRate", "59.940060", "avfoundation"},
        {"track.video.codec", "hvc1", "avfoundation"},
        {"asset.duration", "84.2", "avfoundation"},
    };
    auto n = MetadataNormalizer::normalize(raw);
    EXPECT_EQ(n[MetadataCategory::Camera], "Apple iPhone 15 Pro");
    EXPECT_EQ(n[MetadataCategory::Location], "42.373600, -71.109700");
    EXPECT_EQ(n[MetadataCategory::Time], "2026-10-03T14:22:05-04:00");
    EXPECT_EQ(n[MetadataCategory::Resolution], "3840x2160");
    EXPECT_EQ(n[MetadataCategory::Orientation], "Portrait");
    EXPECT_EQ(n[MetadataCategory::FrameRate], "59.94");
    EXPECT_EQ(n[MetadataCategory::Codec], "HEVC");
    EXPECT_EQ(n[MetadataCategory::Duration], "84.200");
    EXPECT_EQ(n[MetadataCategory::Lens], "iPhone 15 Pro back camera 6.86mm f/1.78");
}

TEST(MetadataNormalizer, GpsLatitudeLongitudeMapToSameLocationCategory) {
    std::vector<RawMetadataEntry> raw = {
        {"exif:GPSLatitude", "42 deg 22' 24.96\"", "xmp"},
        {"exif:GPSLatitudeRef", "N", "xmp"},
        {"exif:GPSLongitude", "71.1097", "xmp"},
        {"exif:GPSLongitudeRef", "W", "xmp"},
        {"udta/%A9mak", "SONY", "udta"},
        {"udta/%A9mod", "ILCE-7M4", "udta"},
    };
    auto n = MetadataNormalizer::normalize(raw);
    EXPECT_EQ(n[MetadataCategory::Location], "42.373600, -71.109700");
    EXPECT_EQ(n[MetadataCategory::Camera], "Sony ILCE-7M4");
}

TEST(MetadataNormalizer, MissingValuesAreAbsentNotGuessed) {
    auto n = MetadataNormalizer::normalize({{"track.video.codec", "apch", "avfoundation"}});
    EXPECT_EQ(n.size(), 1u);
    EXPECT_EQ(n[MetadataCategory::Codec], "Apple ProRes 422 HQ");
    EXPECT_EQ(groupValue(MetadataCategory::Camera, std::nullopt), "Unknown Camera");
}

TEST(MetadataNormalizer, TimeFormats) {
    EXPECT_EQ(MetadataNormalizer::normalizeTime("2026:10:03 14:22:05"), "2026-10-03T14:22:05");
    EXPECT_EQ(MetadataNormalizer::normalizeTime("2026-10-03T14:22:05Z"), "2026-10-03T14:22:05Z");
    EXPECT_EQ(MetadataNormalizer::normalizeTime("2026-10-03T14:22:05.123+02:00"), "2026-10-03T14:22:05+02:00");
    EXPECT_EQ(MetadataNormalizer::normalizeTime("2026-10-03"), "2026-10-03");
    EXPECT_FALSE(MetadataNormalizer::normalizeTime("yesterday"));
}

TEST(MetadataNormalizer, Iso6709DegreeMinuteForms) {
    EXPECT_EQ(MetadataNormalizer::parseISO6709("+4222.416-07106.582/"), "42.373600, -71.109700");
    EXPECT_FALSE(MetadataNormalizer::parseISO6709("garbage"));
}

TEST(MetadataCategory, DisplayAndGrouping) {
    EXPECT_EQ(displayValue(MetadataCategory::FrameRate, "59.94"), "59.94 fps");
    EXPECT_EQ(displayValue(MetadataCategory::Duration, "84.2"), "00:01:24");
    EXPECT_EQ(groupValue(MetadataCategory::Duration, std::string("84.2")), "1-5 min");
    EXPECT_EQ(groupValue(MetadataCategory::Time, std::string("2026-10-03T14:22:05Z")), "2026-10-03");
    EXPECT_EQ(groupValue(MetadataCategory::Location, std::string("42.373600, -71.109700")), "42.374, -71.11");
    EXPECT_EQ(categoryFromKey("frame_rate"), MetadataCategory::FrameRate);
}
