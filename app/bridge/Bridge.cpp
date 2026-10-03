#include <cstdlib>
#include <cstring>

#include "app/application/ImportService.h"
#include "app/application/JsonCodec.h"
#include "app/application/SearchService.h"
#include "app/bridge/Dispatcher.h"
#include "core/commands/EditCommands.h"
#include "core/util/PathUtil.h"
#include "vo_bridge.h"

using nlohmann::json;

namespace vo::bridge {

SessionHandle::~SessionHandle() {
    cancelImport = true;
    joinImport();
}

void SessionHandle::joinImport() {
    std::lock_guard lock(threadMutex);
    if (importThread.joinable()) importThread.join();
}

namespace {

std::vector<std::string> stringList(const json& req, const char* key) {
    std::vector<std::string> out;
    if (req.contains(key) && req[key].is_array())
        for (const auto& v : req[key]) out.push_back(v.get<std::string>());
    return out;
}

std::string str(const json& req, const char* key, const std::string& fallback = "") {
    if (!req.contains(key) || req[key].is_null()) return fallback;
    return req[key].get<std::string>();
}

std::string requireStr(const json& req, const char* key) {
    if (!req.contains(key) || !req[key].is_string()) throw UserError(std::string("Missing argument: ") + key);
    return req[key].get<std::string>();
}

std::string pluralize(size_t n, const char* noun) {
    return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
}

} // namespace

json dispatch(SessionHandle& handle, const json& req) {
    ProjectSession& s = *handle.session;
    const std::string cmd = requireStr(req, "cmd");

    if (cmd == "snapshot") return codec::snapshot(s);

    if (cmd == "clipDetails") {
        std::lock_guard lock(s.mutex());
        return codec::clipDetails(s.model(), requireStr(req, "clipId"));
    }
    if (cmd == "pendingChanges") {
        std::lock_guard lock(s.mutex());
        return codec::changes(s.model(), computeChanges(s.model()));
    }
    if (cmd == "preflight") return codec::preflight(s.preflight());
    if (cmd == "apply") return codec::applyResult(s.apply());
    if (cmd == "refresh") return codec::refreshResult(s.refresh());
    if (cmd == "undo") return {{"done", s.undo()}};
    if (cmd == "redo") return {{"done", s.redo()}};

    if (cmd == "createFolder") {
        std::string name = str(req, "name", "untitled folder");
        auto id = s.edit("Create Folder \"" + name + "\"", [&](ProjectModel& m) {
            return commands::createFolder(m, requireStr(req, "parentId"), name);
        });
        return {{"folderId", id}};
    }
    if (cmd == "renameFolder") {
        std::string name = requireStr(req, "name");
        s.edit("Rename Folder to \"" + name + "\"",
               [&](ProjectModel& m) { commands::renameFolder(m, requireStr(req, "folderId"), name); });
        return {};
    }
    if (cmd == "moveItems") {
        auto clips = stringList(req, "clipIds");
        auto folders = stringList(req, "folderIds");
        std::string dest = requireStr(req, "destinationId");
        size_t n = clips.size() + folders.size();
        s.edit("Move " + pluralize(n, "Item"), [&](ProjectModel& m) {
            // Ignore clips that are inside a folder being moved alongside them.
            std::vector<ClipId> loose;
            for (const auto& cid : clips) {
                const Clip* c = m.clip(cid);
                if (!c) continue;
                bool inside = false;
                for (const auto& fid : folders) inside = inside || m.isAncestorOrSelf(fid, c->folderId);
                if (!inside) loose.push_back(cid);
            }
            if (!folders.empty()) commands::moveFolders(m, folders, dest);
            if (!loose.empty()) commands::moveClips(m, loose, dest);
        });
        return {};
    }
    if (cmd == "deleteFolder") {
        std::string behavior = str(req, "behavior", "moveToParent");
        auto b = behavior == "removeClips" ? commands::DeleteFolderBehavior::RemoveClipsFromProject
                                           : commands::DeleteFolderBehavior::MoveClipsToParent;
        s.edit("Delete Folder", [&](ProjectModel& m) { commands::deleteFolder(m, requireStr(req, "folderId"), b); });
        return {};
    }
    if (cmd == "renameClip") {
        std::string title = requireStr(req, "title");
        s.edit("Rename Clip to \"" + title + "\"",
               [&](ProjectModel& m) { commands::renameClip(m, requireStr(req, "clipId"), title); });
        return {};
    }
    if (cmd == "removeClips") {
        auto clips = stringList(req, "clipIds");
        s.edit("Remove " + pluralize(clips.size(), "Clip") + " from Project",
               [&](ProjectModel& m) { commands::removeClips(m, clips); });
        return {};
    }
    if (cmd == "createTag") {
        std::string name = requireStr(req, "name");
        auto id = s.edit("Create Tag \"" + name + "\"", [&](ProjectModel& m) { return commands::createTag(m, name); });
        return {{"tagId", id}};
    }
    if (cmd == "renameTag") {
        s.edit("Rename Tag", [&](ProjectModel& m) {
            commands::renameTag(m, requireStr(req, "tagId"), requireStr(req, "name"));
        });
        return {};
    }
    if (cmd == "deleteTag") {
        s.edit("Delete Tag", [&](ProjectModel& m) { commands::deleteTag(m, requireStr(req, "tagId")); });
        return {};
    }
    if (cmd == "addTag") {
        auto clips = stringList(req, "clipIds");
        std::string tagName = str(req, "name");
        std::string tagId = str(req, "tagId");
        std::string label = tagName.empty() ? "Tag" : "Tag \"" + tagName + "\"";
        auto id = s.edit("Add " + label, [&](ProjectModel& m) {
            TagId t = tagId.empty() ? commands::createTag(m, tagName) : tagId;
            commands::addTag(m, clips, t);
            return t;
        });
        return {{"tagId", id}};
    }
    if (cmd == "removeTag") {
        auto clips = stringList(req, "clipIds");
        s.edit("Remove Tag", [&](ProjectModel& m) { commands::removeTag(m, clips, requireStr(req, "tagId")); });
        return {};
    }
    if (cmd == "organize") {
        OrganizationConfig cfg;
        cfg.criteria = codec::criteriaFromJson(req.value("criteria", json::array()));
        cfg.namingTemplate = str(req, "namingTemplate");
        s.organize(cfg);
        return {};
    }
    if (cmd == "applyNaming") {
        s.applyNaming(str(req, "namingTemplate"));
        return {};
    }
    if (cmd == "previewNames") {
        // Example output of a template for the first clips of the project,
        // without modifying anything.
        std::lock_guard lock(s.mutex());
        std::string tmpl = str(req, "namingTemplate");
        json out = json::array();
        if (auto err = NamingEngine::validateTemplate(tmpl)) return {{"error", *err}, {"examples", out}};
        std::vector<ClipId> ids;
        for (const auto& [id, c] : s.model().clips()) ids.push_back(id);
        ids = NamingEngine::numberingOrder(s.model(), ids);
        int n = 1;
        for (const auto& id : ids) {
            if (n > 3) break;
            const Clip& c = *s.model().clip(id);
            out.push_back({{"original", c.originalFilename},
                           {"name", NamingEngine::render(tmpl, NamingEngine::contextFor(s.model(), id, n)) + c.extension}});
            ++n;
        }
        return {{"examples", out}};
    }
    if (cmd == "resetToAutomatic") {
        s.resetToAutomatic(stringList(req, "clipIds"));
        return {};
    }
    if (cmd == "resetOrganization") {
        s.resetOrganization();
        return {};
    }
    if (cmd == "search") {
        std::lock_guard lock(s.mutex());
        return codec::searchResult(search(s.model(), str(req, "query")));
    }
    if (cmd == "categoryStats") {
        std::lock_guard lock(s.mutex());
        return codec::categoryStats(s.model());
    }
    if (cmd == "recoveryReport") {
        std::lock_guard lock(s.mutex());
        json ops = json::array();
        for (const auto& op : s.recoveryReport()) ops.push_back(codec::applyOperation(op));
        return {{"operations", ops}};
    }
    if (cmd == "absolutePath") {
        std::lock_guard lock(s.mutex());
        const ProjectModel& m = s.model();
        if (auto cid = str(req, "clipId"); !cid.empty()) {
            const Clip* c = m.clip(cid);
            if (!c) throw UserError("Clip not found");
            return {{"path", path::absolute(m.info.rootPath, c->filePath)}, {"exists", c->status == ClipStatus::Available}};
        }
        const Folder* f = m.folder(requireStr(req, "folderId"));
        if (!f) throw UserError("Folder not found");
        std::string p = f->physicalPath ? *f->physicalPath : "";
        return {{"path", path::absolute(m.info.rootPath, p)}, {"exists", f->physicalPath.has_value()}};
    }
    if (cmd == "startImport") {
        std::string dir = str(req, "path");
        {
            std::lock_guard lock(s.mutex());
            s.ensureNotBusy();
            if (!dir.empty() && dir[0] == '/') {
                std::string rel;
                if (!path::relativeTo(s.model().info.rootPath, dir, rel))
                    throw UserError("Only folders inside the project root (" + s.model().info.rootPath +
                                    ") can be imported.");
            }
        }
        handle.joinImport();
        std::lock_guard lock(handle.threadMutex);
        handle.cancelImport = false;
        handle.importThread = std::thread([&handle, dir] {
            try {
                ImportService::run(*handle.session, dir, &handle.cancelImport);
            } catch (const std::exception& e) {
                handle.session->emit("importFinished", {{"added", 0}, {"error", e.what()}, {"cancelled", false}});
            }
        });
        return {{"started", true}};
    }
    if (cmd == "cancelImport") {
        handle.cancelImport = true;
        return {};
    }
    throw UserError("Unknown command: " + cmd);
}

} // namespace vo::bridge

struct vo_session {
    vo::bridge::SessionHandle handle;
};

namespace {

char* dup(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

vo::ProjectSession::Dependencies defaultDependencies() { return {}; }

} // namespace

extern "C" {

vo_session* vo_session_create(const char* root_path, const char* project_name, char** error_message) {
    try {
        auto* s = new vo_session();
        try {
            s->handle.session = vo::ProjectSession::create(root_path ? root_path : "", project_name ? project_name : "",
                                                           defaultDependencies());
        } catch (...) {
            delete s;
            throw;
        }
        return s;
    } catch (const std::exception& e) {
        if (error_message) *error_message = dup(e.what());
        return nullptr;
    }
}

vo_session* vo_session_open(const char* project_file, char** error_message) {
    try {
        auto* s = new vo_session();
        try {
            s->handle.session = vo::ProjectSession::open(project_file ? project_file : "", defaultDependencies());
        } catch (...) {
            delete s;
            throw;
        }
        return s;
    } catch (const std::exception& e) {
        if (error_message) *error_message = dup(e.what());
        return nullptr;
    }
}

void vo_session_close(vo_session* session) { delete session; }

void vo_session_set_event_callback(vo_session* session, vo_event_callback callback, void* context) {
    if (!session) return;
    if (!callback) {
        session->handle.session->setEventSink({});
        return;
    }
    session->handle.session->setEventSink([callback, context](const std::string& type, const json& payload) {
        std::string event = json{{"type", type}, {"payload", payload}}.dump();
        callback(context, event.c_str());
    });
}

char* vo_session_call(vo_session* session, const char* request_json) {
    json response;
    try {
        if (!session) throw vo::UserError("No project is open");
        json req = json::parse(request_json ? request_json : "{}");
        json result = vo::bridge::dispatch(session->handle, req);
        response = {{"ok", true}, {"result", result.is_null() ? json::object() : result}};
    } catch (const std::exception& e) {
        response = {{"ok", false}, {"error", e.what()}};
    }
    return dup(response.dump(-1, ' ', false, json::error_handler_t::replace));
}

void vo_free_string(char* s) { std::free(s); }

} // extern "C"
