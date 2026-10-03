#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vo {

// Standardized, camera-independent metadata vocabulary (spec §11). New
// categories can be appended without changing the project model: normalized
// values are stored as (category key, value) rows.
enum class MetadataCategory {
    Time,
    Location,
    Camera,
    Lens,
    Resolution,
    FrameRate,
    Codec,
    Author,
    Duration,
    Orientation,
};

const std::vector<MetadataCategory>& allMetadataCategories();
std::string categoryKey(MetadataCategory c);         // stable storage key, e.g. "frame_rate"
std::string categoryDisplayName(MetadataCategory c); // e.g. "Frame Rate"
std::optional<MetadataCategory> categoryFromKey(std::string_view key);

// Canonical normalized values (as produced by MetadataNormalizer):
//   Time        ISO-8601 "2026-10-03T14:22:05-04:00" (or "Z")
//   Location    "42.373600, -71.109700"
//   Camera      "Apple iPhone 15 Pro"
//   Lens        free text
//   Resolution  "3840x2160" (encoded/natural size)
//   FrameRate   "59.94"
//   Codec       "H.264", "HEVC", "Apple ProRes 422 HQ", …
//   Author      free text
//   Duration    seconds, "84.200"
//   Orientation "Landscape" | "Portrait"
using NormalizedMetadata = std::map<MetadataCategory, std::string>;

struct RawMetadataEntry {
    std::string key;
    std::string value;
    std::string source;
    bool operator==(const RawMetadataEntry&) const = default;
};

struct ClipMetadata {
    std::vector<RawMetadataEntry> raw;
    NormalizedMetadata normalized;
    bool operator==(const ClipMetadata&) const = default;
};

// Human-readable presentation of a normalized value ("59.94 fps", "00:01:24").
std::string displayValue(MetadataCategory c, const std::string& value);

// Folder name used by automatic organization for a (possibly missing) value.
// Missing values map to "Unknown <Category>" so clips never disappear from a
// proposal (spec §19, FR-META-007).
std::string groupValue(MetadataCategory c, const std::optional<std::string>& value);

// Date portion ("2026-10-03") of a normalized Time value, if parseable.
std::optional<std::string> datePart(const std::string& timeValue);

} // namespace vo
