#include "app/application/ProjectSession.h"

#include <set>

#include <nlohmann/json.hpp>

#include "core/organization/OrganizationEngine.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"
#include "media/formats/SupportedFormats.h"

namespace vo {

ProjectSession::ProjectSession(Dependencies deps) : deps_(std::move(deps)) {
    if (!deps_.fileSystem) deps_.fileSystem = std::make_shared<PosixFileSystem>();
    if (!deps_.extractor) deps_.extractor = media::makeAVFoundationExtractor();
    store_.attachModel(&model_);
}

ProjectSession::~ProjectSession() { store_.close(); }

std::unique_ptr<ProjectSession> ProjectSession::create(const std::string& rootPathIn, const std::string& projectName,
                                                       Dependencies deps) {
    std::unique_ptr<ProjectSession> s(new ProjectSession(std::move(deps)));
    std::string rootPath = path::normalizeAbsolute(rootPathIn);
    if (!s->fileSystem().isDirectory(rootPath)) throw UserError("The footage folder does not exist: " + rootPath);
    std::string name = path::sanitizeName(trim(projectName), path::filename(rootPath));

    ProjectInfo info;
    info.id = generateId();
    info.name = name;
    info.rootPath = rootPath;
    info.projectFile = path::absolute(rootPath, name + media::kProjectFileExtension);
    info.createdAt = info.updatedAt = nowUnixSeconds();

    Folder root;
    root.id = generateId();
    root.name = name;
    root.origin = FolderOrigin::Imported;
    root.physicalPath = "";

    s->store_.create(info.projectFile, info, root, OrganizationConfig{});
    s->store_.load(s->model_);
    s->model_.info.projectFile = info.projectFile;
    return s;
}

std::unique_ptr<ProjectSession> ProjectSession::open(const std::string& projectFileIn, Dependencies deps) {
    std::unique_ptr<ProjectSession> s(new ProjectSession(std::move(deps)));
    std::string projectFile = path::normalizeAbsolute(projectFileIn);
    if (!s->fileSystem().info(projectFile).isRegularFile) throw UserError("Project file not found: " + projectFile);
    s->store_.open(projectFile);
    s->store_.load(s->model_);

    // The root is always the directory containing the project file, so a
    // footage folder moved as a whole still opens correctly.
    std::string root = path::parent(projectFile);
    s->model_.info.projectFile = projectFile;
    if (s->model_.info.rootPath != root) {
        s->model_.info.rootPath = root;
        s->store_.updateProjectRow(s->model_.info);
    }

    auto pending = s->store_.pendingOperations();
    if (!pending.empty()) {
        s->recoveryReport_ = ApplyEngine::recover(s->model_, s->fileSystem(), s->store_, pending);
    }
    s->refresh();
    return s;
}

void ProjectSession::ensureNotBusy() const {
    if (busy_.load()) throw UserError("Please wait for the current import or apply to finish");
}

void ProjectSession::setEventSink(EventSink sink) {
    std::lock_guard lock(sinkMutex_);
    sink_ = std::move(sink);
}

void ProjectSession::emit(const std::string& type, const nlohmann::json& payload) const {
    EventSink sink;
    {
        std::lock_guard lock(sinkMutex_);
        sink = sink_;
    }
    if (sink) sink(type, payload);
}

void ProjectSession::commitEdit(const std::string& description) {
    DeltaList deltas = model_.takeDeltas();
    if (deltas.empty()) return;
    try {
        store_.persist(model_, deltas);
    } catch (...) {
        model_.revert(deltas);
        model_.takeDeltas();
        throw;
    }
    undo_.push(description, std::move(deltas));
    emit("modelChanged", {{"reason", description}});
}

void ProjectSession::rollbackEdit() {
    DeltaList deltas = model_.takeDeltas();
    model_.revert(deltas);
    model_.takeDeltas();
}

void ProjectSession::commitNonUndoable(bool clearUndoHistory) {
    std::lock_guard lock(mutex_);
    DeltaList deltas = model_.takeDeltas();
    store_.persist(model_, deltas);
    if (clearUndoHistory) undo_.clear();
    if (!deltas.empty() || clearUndoHistory) emit("modelChanged", {{"reason", "physical"}});
}

bool ProjectSession::undo() {
    std::lock_guard lock(mutex_);
    ensureNotBusy();
    std::string desc = undo_.undoDescription();
    if (!undo_.undo(model_)) return false;
    store_.persist(model_, model_.takeDeltas());
    emit("modelChanged", {{"reason", "Undo " + desc}});
    return true;
}

bool ProjectSession::redo() {
    std::lock_guard lock(mutex_);
    ensureNotBusy();
    std::string desc = undo_.redoDescription();
    if (!undo_.redo(model_)) return false;
    store_.persist(model_, model_.takeDeltas());
    emit("modelChanged", {{"reason", "Redo " + desc}});
    return true;
}

DiskOccupancy ProjectSession::diskOccupancy() const {
    auto paths = std::make_shared<std::set<std::string>>();
    for (const auto& [id, c] : model_.clips()) paths->insert(toLower(c.filePath));
    std::string root = model_.info.rootPath;
    std::shared_ptr<FileSystem> fs = deps_.fileSystem;
    std::string projectFile = toLower(path::filename(model_.info.projectFile));
    return [paths, root, fs, projectFile](const std::string& rel) {
        if (toLower(rel) == projectFile) return true;
        if (paths->count(toLower(rel))) return false; // a clip that is moving away
        return fs->exists(path::absolute(root, rel));
    };
}

void ProjectSession::organize(const OrganizationConfig& config) {
    std::string desc = "Organize";
    if (!config.criteria.empty()) {
        std::vector<std::string> names;
        for (auto c : config.criteria) names.push_back(categoryDisplayName(c));
        desc += " by " + join(names, " / ");
    }
    auto occupied = [&] {
        std::lock_guard lock(mutex_);
        return diskOccupancy();
    }();
    edit(desc, [&](ProjectModel& m) { OrganizationEngine::organize(m, config, occupied); });
}

void ProjectSession::applyNaming(const std::string& namingTemplate) {
    auto occupied = [&] {
        std::lock_guard lock(mutex_);
        return diskOccupancy();
    }();
    edit("Apply Naming Template", [&](ProjectModel& m) {
        OrganizationConfig cfg = m.config();
        cfg.namingTemplate = namingTemplate;
        if (auto err = NamingEngine::validateTemplate(namingTemplate)) throw UserError(*err);
        m.setConfig(cfg);
        OrganizationEngine::applyNaming(m, namingTemplate, occupied);
    });
}

void ProjectSession::resetToAutomatic(const std::vector<ClipId>& clips) {
    auto occupied = [&] {
        std::lock_guard lock(mutex_);
        return diskOccupancy();
    }();
    edit("Reset to Automatic Organization",
         [&](ProjectModel& m) { OrganizationEngine::resetToAutomatic(m, clips, occupied); });
}

void ProjectSession::resetOrganization() {
    edit("Reset Organization", [&](ProjectModel& m) { OrganizationEngine::resetOrganization(m); });
}

RefreshResult ProjectSession::refresh() {
    std::lock_guard lock(mutex_);
    RefreshResult r = refreshPhysicalState(model_, fileSystem());
    commitNonUndoable(false);
    return r;
}

ChangeSet ProjectSession::pendingChanges() const {
    std::lock_guard lock(mutex_);
    return computeChanges(model_);
}

PreflightReport ProjectSession::preflight() {
    std::lock_guard lock(mutex_);
    refresh();
    return runPreflight(model_, computeChanges(model_), fileSystem());
}

ApplyResult ProjectSession::apply() {
    std::lock_guard lock(mutex_);
    ensureNotBusy();
    busy_ = true;
    struct BusyReset {
        std::atomic<bool>& flag;
        ~BusyReset() { flag = false; }
    } reset{busy_};

    refreshPhysicalState(model_, fileSystem());
    store_.persist(model_, model_.takeDeltas());
    ChangeSet changes = computeChanges(model_);
    PreflightReport report = runPreflight(model_, changes, fileSystem());
    if (!report.ok) {
        ApplyResult blocked;
        blocked.state = ApplyState::Blocked;
        blocked.preflight = std::move(report);
        return blocked;
    }
    emit("applyStarted", {{"operations", changes.size()}});
    ApplyEngine engine(model_, fileSystem(), store_);
    ApplyResult result = engine.execute();
    result.preflight = std::move(report);

    // Reconcile: whatever actually happened on disk is now the baseline.
    refreshPhysicalState(model_, fileSystem());
    store_.persist(model_, model_.takeDeltas());
    undo_.clear();
    emit("applyFinished", {{"state", toString(result.state)},
                           {"succeeded", result.succeeded},
                           {"failed", result.failed}});
    emit("modelChanged", {{"reason", "Apply"}});
    return result;
}

} // namespace vo
