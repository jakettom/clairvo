#include "app/application/ImportService.h"

#include <algorithm>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/metadata/MetadataNormalizer.h"
#include "core/organization/OrganizationEngine.h"
#include "core/util/PathUtil.h"
#include "media/hashing/FileHasher.h"
#include "media/scanning/DirectoryScanner.h"

namespace vo {

namespace {

constexpr size_t kBatchSize = 250;

template <class Fn>
void parallelFor(size_t count, const std::atomic<bool>* cancel, Fn&& fn) {
    size_t workers = std::clamp<size_t>(std::thread::hardware_concurrency(), 1, 8);
    workers = std::min(workers, std::max<size_t>(count, 1));
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    for (size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            while (true) {
                if (cancel && cancel->load()) return;
                size_t i = next.fetch_add(1);
                if (i >= count) return;
                fn(i);
            }
        });
    }
    for (auto& t : threads) t.join();
}

} // namespace

ImportSummary ImportService::run(ProjectSession& session, const std::string& directory, const std::atomic<bool>* cancel) {
    ImportSummary summary;
    std::string root;
    std::string rel;
    {
        std::lock_guard lock(session.mutex());
        session.ensureNotBusy();
        root = session.model().info.rootPath;
        if (directory.empty()) {
            rel.clear();
        } else if (directory[0] == '/') {
            if (!path::relativeTo(root, directory, rel))
                throw UserError("Only folders inside the project root can be imported. Footage belongs to a project "
                                "by its physical location.");
        } else {
            rel = directory;
        }
        session.busy() = true;
    }
    struct BusyReset {
        std::atomic<bool>& flag;
        ~BusyReset() { flag = false; }
    } reset{session.busy()};

    auto progress = [&](const char* phase, size_t done, size_t total) {
        session.emit("importProgress", {{"phase", phase}, {"completed", done}, {"total", total}});
    };

    // 1. Scanning
    progress("scanning", 0, 0);
    media::ScanResult scan = media::scanDirectory(
        root, rel, [&](size_t found) { progress("scanning", found, 0); }, cancel);
    summary.scanned = scan.files.size();
    summary.skippedUnsupported = scan.skippedUnsupported;
    for (const auto& d : scan.unreadableDirectories) summary.errors.push_back("Could not read folder: " + d);

    std::vector<media::ScannedFile> files;
    {
        std::lock_guard lock(session.mutex());
        for (auto& f : scan.files) {
            if (session.model().clipWithFilePath(f.relativePath)) ++summary.alreadyInProject;
            else files.push_back(std::move(f));
        }
    }

    // 2. Hashing
    std::vector<std::string> hashes(files.size());
    std::atomic<size_t> done{0};
    progress("hashing", 0, files.size());
    parallelFor(files.size(), cancel, [&](size_t i) {
        hashes[i] = media::fingerprintFile(path::absolute(root, files[i].relativePath)).value_or("");
        size_t n = ++done;
        if (n % 10 == 0 || n == files.size()) progress("hashing", n, files.size());
    });

    // 3. Metadata extraction + normalization
    std::vector<std::shared_ptr<ClipMetadata>> metadata(files.size());
    done = 0;
    progress("metadata", 0, files.size());
    parallelFor(files.size(), cancel, [&](size_t i) {
        auto md = std::make_shared<ClipMetadata>();
        md->raw = session.extractor().extract(path::absolute(root, files[i].relativePath));
        md->normalized = MetadataNormalizer::normalize(md->raw);
        metadata[i] = std::move(md);
        size_t n = ++done;
        if (n % 10 == 0 || n == files.size()) progress("metadata", n, files.size());
    });

    if (cancel && cancel->load()) {
        summary.cancelled = true;
        session.emit("importFinished", {{"added", 0}, {"cancelled", true}});
        return summary;
    }

    // 4. Database insertion in batches
    progress("database", 0, files.size());
    for (size_t start = 0; start < files.size(); start += kBatchSize) {
        std::lock_guard lock(session.mutex());
        ProjectModel& m = session.mutableModel();
        size_t end = std::min(files.size(), start + kBatchSize);
        for (size_t i = start; i < end; ++i) {
            const auto& f = files[i];
            if (m.clipWithFilePath(f.relativePath)) continue;
            std::string name = path::filename(f.relativePath);
            std::string ext = path::extension(name);
            Clip c;
            c.id = generateId();
            c.folderId = OrganizationEngine::ensurePhysicalFolder(m, path::parent(f.relativePath));
            c.filePath = f.relativePath;
            c.originalFilename = name;
            c.title = name.substr(0, name.size() - ext.size());
            c.extension = ext;
            c.fileHash = hashes[i];
            c.fileSize = static_cast<int64_t>(f.size);
            c.status = ClipStatus::Available;
            c.createdAt = c.updatedAt = nowUnixSeconds();
            m.putClip(c);
            m.setClipMetadata(c.id, metadata[i]);
            ++summary.added;
        }
        session.commitNonUndoable(false);
        progress("database", end, files.size());
    }
    // The physical baseline gained clips/folders; old undo entries no longer apply.
    session.commitNonUndoable(true);
    session.emit("importFinished", {{"added", summary.added},
                                    {"scanned", summary.scanned},
                                    {"alreadyInProject", summary.alreadyInProject},
                                    {"skippedUnsupported", summary.skippedUnsupported},
                                    {"errors", summary.errors},
                                    {"cancelled", false}});
    return summary;
}

} // namespace vo
