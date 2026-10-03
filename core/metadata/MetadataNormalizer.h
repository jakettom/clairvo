#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/metadata/MetadataCategory.h"

namespace vo {

// Maps camera/container-specific raw keys onto the standard categories
// (spec §10.2). Raw keys are matched case-insensitively by suffix so that the
// same rule covers e.g. "mdta/com.apple.quicktime.make" and "udta/%A9mak".
//
// Extractors additionally emit a few synthetic technical keys:
//   asset.duration        seconds
//   asset.creationDate    ISO-8601
//   track.video.width / track.video.height   natural (encoded) size
//   track.video.frameRate nominal fps
//   track.video.codec     FourCC, e.g. "avc1", "hvc1", "apch"
//   track.video.rotation  degrees from the preferred transform
class MetadataNormalizer {
public:
    static NormalizedMetadata normalize(const std::vector<RawMetadataEntry>& raw);

    // Exposed for testing.
    static std::optional<std::string> normalizeTime(const std::string& value);
    static std::optional<std::string> parseISO6709(const std::string& value);
    static std::string codecName(const std::string& fourcc);
    static std::string formatFrameRate(double fps);
};

} // namespace vo
