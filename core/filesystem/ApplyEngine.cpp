#include "core/filesystem/ApplyEngine.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

std::string toString(ApplyState s) {
    switch (s) {
    case ApplyState::Applied: return "APPLIED";
    case ApplyState::PartiallyApplied: return "PARTIALLY_APPLIED";
    case ApplyState::Failed: return "FAILED";
    case ApplyState::NothingToApply: return "NOTHING_TO_APPLY";
    case ApplyState::Blocked: return "BLOCKED";
    }
    return "FAILED";
}

ApplyEngine::ApplyEngine(ProjectModel& model, FileSystem& fs, ApplyJournal& journal)
    : m_(model), fs_(fs), journal_(journal) {}

void ApplyEngine::relocatePhysicalPrefix(ProjectModel& m, const std::string& from, const std::string& to) {
    std::vector<FolderId> folders;
    for (const auto& [id, f] : m.folders())
        if (f.physicalPath && path::isWithin(*f.physicalPath, from)) folders.push_back(id);
    std::sort(folders.begin(), folders.end());
    for (const auto& id : folders) {
        Folder f = *m.folder(id);
        f.physicalPath = path::rebase(*f.physicalPath, from, to);
        m.putFolder(f);
    }
    std::vector<ClipId> clips;
    for (const auto& [id, c] : m.clips())
        if (path::isWithin(c.filePath, from)) clips.push_back(id);
    std::sort(clips.begin(), clips.end());
    for (const auto& id : clips) {
        Clip c = *m.clip(id);
        c.filePath = path::rebase(c.filePath, from, to);
        c.updatedAt = nowUnixSeconds();
        m.putClip(c);
    }
}

std::string ApplyEngine::stagingName(const std::string& relPath) const {
    return ".clairvo-staging-" + generateId().substr(0, 8) + path::extension(path::filename(relPath));
}

bool ApplyEngine::run(const Op& op, const std::function<std::optional<FsError>()>& action,
                      const std::function<void()>& onSuccess, bool) {
    int64_t opId = journal_.recordPending(batchId_, ++seq_, op.type, op.clipId, op.folderId, op.source, op.destination);
    std::optional<FsError> err = action();
    ApplyOperationResult r;
    r.type = op.type;
    r.clipId = op.clipId;
    r.folderId = op.folderId;
    r.source = op.source;
    r.destination = op.destination;
    r.description = op.description;
    if (!err) {
        onSuccess();
        journal_.recordResult(opId, true, "", m_.takeDeltas());
        r.success = true;
        ++result_.succeeded;
    } else {
        m_.takeDeltas();
        journal_.recordResult(opId, false, err->message, {});
        r.error = err->message;
        ++result_.failed;
    }
    result_.operations.push_back(std::move(r));
    return !err;
}

void ApplyEngine::applyFolders(const ChangeSet& changes) {
    const std::string& root = m_.info.rootPath;
    auto abs = [&](const std::string& rel) { return path::absolute(root, rel); };

    for (const auto& change : changes.folderChanges) {
        const Folder* f = m_.folder(change.folderId);
        if (!f) continue;
        const FolderId id = f->id;
        const std::string intended = m_.folderPath(id);

        if (!f->physicalPath) {
            Op op{"CREATE_FOLDER", "", id, "", intended, "Create folder " + intended + "/"};
            run(
                op,
                [&]() -> std::optional<FsError> {
                    auto e = fs_.createDirectory(abs(intended));
                    if (e && e->kind == FsErrorKind::AlreadyExists && fs_.isDirectory(abs(intended))) return std::nullopt;
                    return e;
                },
                [&] {
                    Folder nf = *m_.folder(id);
                    nf.physicalPath = intended;
                    m_.putFolder(nf);
                });
            continue;
        }

        const std::string src = *f->physicalPath; // already reflects earlier relocations
        if (src == intended) continue;
        const std::string type = path::parent(src) != path::parent(intended) ? "MOVE_FOLDER" : "RENAME_FOLDER";
        std::string from = src;
        if (equalsIgnoreCase(src, intended)) {
            // Case-only rename on a case-insensitive volume: go through a staging name.
            std::string tmp = path::join(path::parent(src), stagingName(src));
            Op stage{"STAGE", "", id, src, tmp, "Stage folder " + src + "/"};
            if (!run(stage, [&] { return fs_.moveNoReplace(abs(src), abs(tmp)); },
                     [&] { relocatePhysicalPrefix(m_, src, tmp); }))
                continue;
            from = tmp;
        }
        Op op{type, "", id, from, intended,
              (type == "MOVE_FOLDER" ? "Move folder " : "Rename folder ") + src + "/ → " + intended + "/"};
        bool ok = run(op, [&] { return fs_.moveNoReplace(abs(from), abs(intended)); },
                      [&] { relocatePhysicalPrefix(m_, from, intended); });
        if (ok) vacatedDirectories_.push_back(path::parent(src));
    }
}

void ApplyEngine::applyClips() {
    const std::string& root = m_.info.rootPath;
    auto abs = [&](const std::string& rel) { return path::absolute(root, rel); };

    struct Move {
        ClipId id;
        std::string src;
        std::string dst;
    };
    std::vector<Move> pending;
    std::map<std::string, ClipId> occupiedBy; // lowercased current path of a pending clip
    ChangeSet cs = computeChanges(m_);
    for (const auto& c : cs.clipChanges) {
        const Clip* clip = m_.clip(c.clipId);
        if (!clip) continue;
        pending.push_back({c.clipId, clip->filePath, c.destinationPath});
        occupiedBy[toLower(clip->filePath)] = c.clipId;
    }

    auto setClipPath = [&](const ClipId& id, const std::string& p) {
        Clip c = *m_.clip(id);
        c.filePath = p;
        c.updatedAt = nowUnixSeconds();
        m_.putClip(c);
    };

    auto execute = [&](Move mv) {
        const std::string originalSrc = mv.src;
        const bool moves = path::parent(mv.src) != path::parent(mv.dst);
        const std::string type = moves ? "MOVE_CLIP" : "RENAME_CLIP";
        if (equalsIgnoreCase(mv.src, mv.dst)) {
            std::string tmp = path::join(path::parent(mv.src), stagingName(mv.src));
            Op stage{"STAGE", mv.id, "", mv.src, tmp, "Stage " + path::filename(mv.src)};
            if (!run(stage, [&] { return fs_.moveNoReplace(abs(mv.src), abs(tmp)); },
                     [&] { setClipPath(mv.id, tmp); }))
                return;
            mv.src = tmp;
        }
        std::string desc = moves ? "Move " + originalSrc + " → " + mv.dst
                                 : "Rename " + path::filename(originalSrc) + " → " + path::filename(mv.dst);
        Op op{type, mv.id, "", mv.src, mv.dst, desc};
        bool ok = run(op, [&] { return fs_.moveNoReplace(abs(mv.src), abs(mv.dst)); },
                      [&] { setClipPath(mv.id, mv.dst); });
        if (ok && moves) vacatedDirectories_.push_back(path::parent(originalSrc));
    };

    while (!pending.empty()) {
        bool progressed = false;
        for (size_t i = 0; i < pending.size();) {
            const Move& mv = pending[i];
            auto blocker = occupiedBy.find(toLower(mv.dst));
            if (blocker != occupiedBy.end() && blocker->second != mv.id) {
                ++i; // destination still holds another clip that has yet to move away
                continue;
            }
            Move current = mv;
            occupiedBy.erase(toLower(current.src));
            pending.erase(pending.begin() + static_cast<long>(i));
            execute(current);
            progressed = true;
        }
        if (progressed || pending.empty()) continue;

        // Every remaining move waits on another: a swap/cycle. Break it by
        // moving one file to a staging name in its own directory.
        Move& mv = pending.front();
        std::string tmp = path::join(path::parent(mv.src), stagingName(mv.src));
        Op stage{"STAGE", mv.id, "", mv.src, tmp, "Stage " + path::filename(mv.src) + " (resolve name cycle)"};
        std::string oldSrc = mv.src;
        ClipId id = mv.id;
        bool ok = run(stage, [&] { return fs_.moveNoReplace(abs(oldSrc), abs(tmp)); }, [&] { setClipPath(id, tmp); });
        if (ok) {
            occupiedBy.erase(toLower(oldSrc));
            occupiedBy[toLower(tmp)] = id;
            mv.src = tmp;
        } else {
            // Leave the file where it is; moves targeting it will fail safely.
            pending.erase(pending.begin());
        }
    }
}

void ApplyEngine::cleanupDirectories() {
    const std::string& root = m_.info.rootPath;
    std::set<std::string> candidates(vacatedDirectories_.begin(), vacatedDirectories_.end());
    std::set<std::string> done;
    for (const auto& start : candidates) {
        std::string dir = start;
        while (!dir.empty() && !done.count(dir)) {
            done.insert(dir);
            if (m_.folderWithPhysicalPath(dir)) break; // still represented in the project
            FileInfo fi = fs_.info(path::absolute(root, dir));
            if (fi.exists) {
                if (!fi.isDirectory) break;
                if (fs_.removeEmptyDirectory(path::absolute(root, dir))) break; // not empty
                result_.removedDirectories.push_back(dir);
            }
            dir = path::parent(dir);
        }
    }
}

ApplyResult ApplyEngine::execute() {
    result_ = ApplyResult{};
    vacatedDirectories_.clear();
    ChangeSet changes = computeChanges(m_);
    if (changes.empty()) {
        result_.state = ApplyState::NothingToApply;
        return result_;
    }
    batchId_ = journal_.beginBatch();
    seq_ = 0;
    applyFolders(changes);
    applyClips();
    cleanupDirectories();

    if (result_.failed == 0) result_.state = result_.succeeded > 0 ? ApplyState::Applied : ApplyState::NothingToApply;
    else result_.state = result_.succeeded > 0 ? ApplyState::PartiallyApplied : ApplyState::Failed;
    journal_.finishBatch(batchId_, toString(result_.state));
    return result_;
}

std::vector<ApplyOperationResult> ApplyEngine::recover(ProjectModel& m, FileSystem& fs, ApplyJournal& journal,
                                                       const std::vector<PendingOperation>& pending) {
    std::vector<ApplyOperationResult> out;
    const std::string& root = m.info.rootPath;
    auto abs = [&](const std::string& rel) { return path::absolute(root, rel); };
    std::set<std::string> batches;

    for (const auto& op : pending) {
        batches.insert(op.batchId);
        ApplyOperationResult r;
        r.type = op.type;
        r.clipId = op.clipId;
        r.folderId = op.folderId;
        r.source = op.source;
        r.destination = op.destination;
        bool completed = false;
        if (op.type == "CREATE_FOLDER") {
            completed = fs.isDirectory(abs(op.destination));
            if (completed) {
                if (const Folder* f = m.folder(op.folderId); f && !f->physicalPath) {
                    Folder nf = *f;
                    nf.physicalPath = op.destination;
                    m.putFolder(nf);
                }
            }
        } else {
            completed = !fs.exists(abs(op.source)) && fs.exists(abs(op.destination));
            if (completed) {
                if (!op.folderId.empty()) {
                    relocatePhysicalPrefix(m, op.source, op.destination);
                } else if (const Clip* c = m.clip(op.clipId); c && equalsIgnoreCase(c->filePath, op.source)) {
                    Clip nc = *c;
                    nc.filePath = op.destination;
                    m.putClip(nc);
                }
            }
        }
        r.success = completed;
        r.error = completed ? "" : "Interrupted before completion; file left at its original location";
        r.description = (completed ? "Recovered: " : "Not performed: ") + op.type + " " +
                        (op.source.empty() ? "" : op.source + " → ") + op.destination;
        journal.recordResult(op.id, completed, r.error, m.takeDeltas());
        out.push_back(std::move(r));
    }
    for (const auto& b : batches) journal.finishBatch(b, "INTERRUPTED");
    return out;
}

} // namespace vo
