#pragma once

#include <optional>
#include <string>

namespace vo::media {

// File fingerprint (spec §48): SHA-256 over the file size plus the first and
// last 4 MiB. Fast on multi-GB camera files while still detecting identity
// changes; it is never used as the clip's identity.
std::optional<std::string> fingerprintFile(const std::string& absolutePath);

constexpr size_t kFingerprintChunkBytes = 4 * 1024 * 1024;

} // namespace vo::media
