#include "media/scanning/DirectoryScanner.h"

#include <algorithm>
#include <filesystem>

#include <sys/stat.h>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"
#include "media/formats/SupportedFormats.h"

namespace fs = std::filesystem;

namespace vo::media {

ScanResult scanDirectory(const std::string& root, const std::string& startRel,
                         const std::function<void(size_t)>& progress, const std::atomic<bool>* cancel) {
    ScanResult result;
    std::vector<std::string> stack{startRel};
    while (!stack.empty()) {
        if (cancel && cancel->load()) break;
        std::string rel = stack.back();
        stack.pop_back();
        std::error_code ec;
        fs::directory_iterator it(path::absolute(root, rel), fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            result.unreadableDirectories.push_back(rel);
            continue;
        }
        for (const auto& entry : it) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code sec;
            auto status = entry.symlink_status(sec);
            if (sec || fs::is_symlink(status)) continue;
            std::string childRel = path::join(rel, name);
            if (fs::is_directory(status)) {
                if (!isOpaquePackageDirectory(name)) stack.push_back(childRel);
                continue;
            }
            if (!fs::is_regular_file(status)) continue;
            if (endsWithIgnoreCase(name, kProjectFileExtension)) continue;
            if (!isSupportedVideoFile(name)) {
                ++result.skippedUnsupported;
                continue;
            }
            ScannedFile f;
            f.relativePath = childRel;
            f.size = static_cast<uint64_t>(entry.file_size(sec));
            struct stat st{};
            if (::lstat(entry.path().c_str(), &st) == 0) f.modifiedAt = static_cast<int64_t>(st.st_mtimespec.tv_sec);
            result.files.push_back(std::move(f));
            if (progress && result.files.size() % 50 == 0) progress(result.files.size());
        }
    }
    std::sort(result.files.begin(), result.files.end(), [](const ScannedFile& a, const ScannedFile& b) {
        return naturalLess(a.relativePath, b.relativePath);
    });
    if (progress) progress(result.files.size());
    return result;
}

} // namespace vo::media
