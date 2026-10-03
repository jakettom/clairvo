#include "support/TestSupport.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <unistd.h>

#include "core/metadata/MetadataNormalizer.h"
#include "core/organization/OrganizationEngine.h"
#include "core/util/PathUtil.h"

namespace fs = std::filesystem;

namespace vo::test {

TempDir::TempDir() {
    std::string tmpl = (fs::temp_directory_path() / "clairvo-test-XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* p = ::mkdtemp(buf.data());
    // Resolve /var -> /private/var so paths compare equal to realpath results.
    path_ = fs::canonical(p).string();
}

TempDir::~TempDir() {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(path_, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
    }
    fs::permissions(path_, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(path_, ec);
}

void TempDir::write(const std::string& rel, const std::string& contents) const {
    fs::create_directories(fs::path(file(rel)).parent_path());
    std::ofstream out(file(rel), std::ios::binary);
    out << contents;
}

void TempDir::mkdir(const std::string& rel) const { fs::create_directories(file(rel)); }

bool TempDir::exists(const std::string& rel) const { return fs::exists(file(rel)); }

std::string TempDir::read(const std::string& rel) const {
    std::ifstream in(file(rel), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> TempDir::listRecursive() const {
    std::vector<std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(path_)) {
        if (!e.is_regular_file()) continue;
        out.push_back(fs::relative(e.path(), path_).string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

ModelBuilder::ModelBuilder(ProjectModel& model, const std::string& root, const std::string& name) : m(model) {
    m.info.id = generateId();
    m.info.name = name;
    m.info.rootPath = root;
    m.info.projectFile = root + "/" + name + ".project";
    Folder r;
    r.id = generateId();
    r.name = name;
    r.origin = FolderOrigin::Imported;
    r.physicalPath = "";
    m.loadFolder(r);
}

FolderId ModelBuilder::folder(const std::string& relPath, FolderOrigin origin) {
    FolderId id = OrganizationEngine::ensurePhysicalFolder(m, relPath);
    if (origin != FolderOrigin::Imported) {
        Folder f = *m.folder(id);
        f.origin = origin;
        m.putFolder(f);
    }
    m.takeDeltas();
    return id;
}

ClipId ModelBuilder::clip(const std::string& relPath, std::map<MetadataCategory, std::string> md) {
    Clip c;
    c.id = generateId();
    c.folderId = OrganizationEngine::ensurePhysicalFolder(m, path::parent(relPath));
    c.filePath = relPath;
    c.originalFilename = path::filename(relPath);
    c.extension = path::extension(c.originalFilename);
    c.title = path::stem(c.originalFilename);
    m.putClip(c);
    auto meta = std::make_shared<ClipMetadata>();
    for (auto& [k, v] : md) meta->normalized[k] = v;
    m.setClipMetadata(c.id, meta);
    m.takeDeltas();
    return c.id;
}

std::vector<RawMetadataEntry> FakeExtractor::extract(const std::string& absolutePath) {
    auto it = byFilename.find(path::filename(absolutePath));
    return it == byFilename.end() ? std::vector<RawMetadataEntry>{} : it->second;
}

int64_t MemoryJournal::recordPending(const std::string&, int, const std::string& type, const ClipId&, const FolderId&,
                                     const std::string& source, const std::string& destination) {
    entries.push_back({type, source, destination, "PENDING", ""});
    return static_cast<int64_t>(entries.size() - 1);
}

void MemoryJournal::recordResult(int64_t opId, bool success, const std::string& error, const DeltaList&) {
    entries[static_cast<size_t>(opId)].status = success ? "SUCCESS" : "FAILED";
    entries[static_cast<size_t>(opId)].error = error;
}

std::optional<FsError> FaultyFileSystem::moveNoReplace(const std::string& source, const std::string& destination) {
    for (const auto& marker : failMovesInvolving) {
        if (source.find(marker) != std::string::npos || destination.find(marker) != std::string::npos)
            return FsError{FsErrorKind::PermissionDenied, EACCES, "Permission denied"};
    }
    return PosixFileSystem::moveNoReplace(source, destination);
}

std::vector<RawMetadataEntry> rawFor(const std::string& make, const std::string& model, int width, int height,
                                     double fps, const std::string& codec) {
    std::vector<RawMetadataEntry> raw;
    if (!make.empty()) raw.push_back({"mdta/com.apple.quicktime.make", make, "test"});
    if (!model.empty()) raw.push_back({"mdta/com.apple.quicktime.model", model, "test"});
    if (width > 0) {
        raw.push_back({"track.video.width", std::to_string(width), "test"});
        raw.push_back({"track.video.height", std::to_string(height), "test"});
    }
    if (fps > 0) raw.push_back({"track.video.frameRate", std::to_string(fps), "test"});
    if (!codec.empty()) raw.push_back({"track.video.codec", codec, "test"});
    return raw;
}

ProjectSession::Dependencies fakeDependencies(std::shared_ptr<FakeExtractor> extractor, std::shared_ptr<FileSystem> fs) {
    ProjectSession::Dependencies d;
    d.extractor = extractor ? extractor : std::make_shared<FakeExtractor>();
    d.fileSystem = fs ? fs : std::make_shared<PosixFileSystem>();
    return d;
}

} // namespace vo::test
