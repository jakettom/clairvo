#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/metadata/MetadataCategory.h"

namespace vo::media {

// Extraction is isolated behind this interface (spec §59) so the core and
// tests never depend on a particular media framework.
class MetadataExtractor {
public:
    virtual ~MetadataExtractor() = default;
    // Returns raw key/value entries; absent metadata simply yields fewer
    // entries. Must be safe to call concurrently for different files.
    virtual std::vector<RawMetadataEntry> extract(const std::string& absolutePath) = 0;
};

// AVFoundation-backed extractor (QuickTime/MPEG-4 metadata, track format
// descriptions, GPS ISO-6709, creation dates).
std::shared_ptr<MetadataExtractor> makeAVFoundationExtractor();

} // namespace vo::media
