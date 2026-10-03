#include "media/formats/SupportedFormats.h"

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo::media {

const std::vector<std::string>& supportedVideoExtensions() {
    static const std::vector<std::string> exts = {".mov", ".mp4", ".m4v"};
    return exts;
}

bool isSupportedVideoFile(const std::string& filename) {
    if (filename.empty() || filename[0] == '.') return false; // hidden + AppleDouble "._" files
    std::string ext = toLower(path::extension(filename));
    for (const auto& e : supportedVideoExtensions())
        if (ext == e) return true;
    return false;
}

bool isOpaquePackageDirectory(const std::string& dirname) {
    static const std::vector<std::string> packages = {
        ".app", ".fcpbundle", ".imovielibrary", ".photoslibrary", ".prproj", ".dvr", ".lrdata",
        ".bundle", ".framework", ".pkg", ".fcpxmld", ".tvlibrary", ".musiclibrary",
    };
    std::string ext = toLower(path::extension(dirname));
    for (const auto& p : packages)
        if (ext == p) return true;
    return false;
}

} // namespace vo::media
