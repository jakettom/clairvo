#pragma once

#include <string>
#include <vector>

namespace vo::media {

// Video container extensions imported as clips (spec §58). Unsupported files
// are never silently imported.
const std::vector<std::string>& supportedVideoExtensions(); // lowercase, with dot
bool isSupportedVideoFile(const std::string& filename);

// Directory names the scanner never descends into: application packages and
// editor libraries whose internals must not be reorganized.
bool isOpaquePackageDirectory(const std::string& dirname);

constexpr const char* kProjectFileExtension = ".project";

} // namespace vo::media
