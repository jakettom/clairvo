#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/application/ProjectSession.h"
#include "core/filesystem/ApplyEngine.h"
#include "core/filesystem/FileSystem.h"
#include "core/model/ProjectModel.h"
#include "media/extraction/MetadataExtractor.h"

namespace vo::test {

// Temporary directory removed on destruction (restores permissions first).
class TempDir {
public:
    TempDir();
    ~TempDir();
    const std::string& path() const { return path_; }
    std::string file(const std::string& rel) const { return path_ + "/" + rel; }
    void write(const std::string& rel, const std::string& contents = "data") const;
    void mkdir(const std::string& rel) const;
    bool exists(const std::string& rel) const;
    std::string read(const std::string& rel) const;
    std::vector<std::string> listRecursive() const; // relative file paths, sorted

private:
    std::string path_;
};

// In-memory model builder for unit tests.
struct ModelBuilder {
    ProjectModel& m;
    explicit ModelBuilder(ProjectModel& model, const std::string& root = "/footage", const std::string& name = "Doc");
    FolderId folder(const std::string& relPath, FolderOrigin origin = FolderOrigin::Imported); // physical folder chain
    ClipId clip(const std::string& relPath, std::map<MetadataCategory, std::string> md = {});
};

// Extractor that serves canned raw metadata keyed by filename.
class FakeExtractor : public media::MetadataExtractor {
public:
    std::map<std::string, std::vector<RawMetadataEntry>> byFilename;
    std::vector<RawMetadataEntry> extract(const std::string& absolutePath) override;
};

// Journal that only records results (for engine tests without SQLite).
class MemoryJournal : public ApplyJournal {
public:
    struct Entry {
        std::string type, source, destination, status, error;
    };
    std::vector<Entry> entries;
    std::string beginBatch() override { return "batch"; }
    int64_t recordPending(const std::string&, int, const std::string& type, const ClipId&, const FolderId&,
                          const std::string& source, const std::string& destination) override;
    void recordResult(int64_t opId, bool success, const std::string& error, const DeltaList&) override;
    void finishBatch(const std::string&, const std::string&) override {}
};

// Filesystem wrapper that fails moves whose destination or source contains a marker.
class FaultyFileSystem : public PosixFileSystem {
public:
    std::set<std::string> failMovesInvolving;
    std::optional<FsError> moveNoReplace(const std::string& source, const std::string& destination) override;
};

std::vector<RawMetadataEntry> rawFor(const std::string& make, const std::string& model, int width, int height,
                                     double fps, const std::string& codec = "avc1");

ProjectSession::Dependencies fakeDependencies(std::shared_ptr<FakeExtractor> extractor = nullptr,
                                              std::shared_ptr<FileSystem> fs = nullptr);

// Writes a tiny H.264 QuickTime movie with the given metadata using AVAssetWriter.
bool writeTestMovie(const std::string& path, int width, int height, const std::string& make,
                    const std::string& model, const std::string& iso6709, const std::string& creationDate);

} // namespace vo::test
