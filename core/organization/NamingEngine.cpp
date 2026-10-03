#include "core/organization/NamingEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "core/commands/EditCommands.h"
#include "core/util/PathUtil.h"
#include "core/util/StringUtil.h"

namespace vo {

namespace {

struct Token {
    std::string name;
    int width = 3;
};

// Parses "{name}" or "{name:N}". Returns false on malformed input.
bool parseToken(const std::string& body, Token& out) {
    auto colon = body.find(':');
    out.name = body.substr(0, colon);
    if (colon != std::string::npos) {
        std::string w = body.substr(colon + 1);
        if (w.empty() || w.size() > 2 || !std::all_of(w.begin(), w.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) return false;
        out.width = std::stoi(w);
        if (out.width < 1 || out.width > 9) return false;
        if (out.name != "number") return false;
    }
    return true;
}

bool isKnown(const std::string& name) {
    static const std::set<std::string> known = {"folder", "number", "original", "camera", "resolution", "date"};
    return known.count(name) > 0;
}

std::string lowerTitleKey(const std::string& title) { return toLower(title); }

} // namespace

std::vector<std::string> NamingEngine::supportedVariables() {
    return {"{folder}", "{number}", "{original}", "{camera}", "{resolution}", "{date}"};
}

std::optional<std::string> NamingEngine::validateTemplate(const std::string& tmpl) {
    size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] == '}') return "Unbalanced '}' in naming template";
        if (tmpl[i] != '{') {
            ++i;
            continue;
        }
        auto close = tmpl.find('}', i);
        if (close == std::string::npos) return "Unclosed '{' in naming template";
        Token t;
        std::string body = tmpl.substr(i + 1, close - i - 1);
        if (!parseToken(body, t) || !isKnown(t.name)) return "Unknown naming variable {" + body + "}";
        i = close + 1;
    }
    return std::nullopt;
}

bool NamingEngine::usesNumber(const std::string& tmpl) {
    return tmpl.find("{number}") != std::string::npos || tmpl.find("{number:") != std::string::npos;
}

std::string NamingEngine::render(const std::string& tmpl, const NamingContext& ctx) {
    std::string out;
    size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] != '{') {
            out += tmpl[i++];
            continue;
        }
        auto close = tmpl.find('}', i);
        if (close == std::string::npos) {
            out += tmpl.substr(i);
            break;
        }
        Token t;
        std::string body = tmpl.substr(i + 1, close - i - 1);
        if (!parseToken(body, t) || !isKnown(t.name)) {
            out += tmpl.substr(i, close - i + 1);
        } else if (t.name == "folder") {
            out += ctx.folder;
        } else if (t.name == "number") {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%0*d", t.width, ctx.number);
            out += buf;
        } else if (t.name == "original") {
            out += ctx.original;
        } else if (t.name == "camera") {
            out += groupValue(MetadataCategory::Camera, ctx.camera);
        } else if (t.name == "resolution") {
            out += groupValue(MetadataCategory::Resolution, ctx.resolution);
        } else if (t.name == "date") {
            out += ctx.date.value_or("Undated");
        }
        i = close + 1;
    }
    return path::sanitizeName(out, ctx.original.empty() ? "Untitled" : ctx.original);
}

NamingContext NamingEngine::contextFor(const ProjectModel& m, const ClipId& id, int number) {
    NamingContext ctx;
    const Clip* c = m.clip(id);
    if (!c) return ctx;
    const Folder* f = m.folder(c->folderId);
    ctx.folder = (f && !f->parentId.empty()) ? f->name : m.info.name;
    ctx.number = number;
    ctx.original = path::stem(c->originalFilename);
    ctx.camera = m.normalizedValue(id, MetadataCategory::Camera);
    ctx.resolution = m.normalizedValue(id, MetadataCategory::Resolution);
    if (auto t = m.normalizedValue(id, MetadataCategory::Time)) ctx.date = datePart(*t);
    return ctx;
}

std::vector<ClipId> NamingEngine::numberingOrder(const ProjectModel& m, std::vector<ClipId> clips) {
    struct Key {
        std::string time;
        std::string original;
        ClipId id;
    };
    std::vector<Key> keys;
    keys.reserve(clips.size());
    for (const auto& id : clips) {
        auto t = m.normalizedValue(id, MetadataCategory::Time);
        keys.push_back({t.value_or("~"), m.clip(id)->originalFilename, id});
    }
    std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
        if (a.time != b.time) return a.time < b.time;
        if (a.original != b.original) return naturalLess(a.original, b.original);
        return a.id < b.id;
    });
    std::vector<ClipId> out;
    out.reserve(keys.size());
    for (auto& k : keys) out.push_back(std::move(k.id));
    return out;
}

void NamingEngine::applyNaming(ProjectModel& m, const std::string& tmpl, const std::vector<FolderId>& folders,
                               const DiskOccupancy& occupied) {
    if (trim(tmpl).empty()) return;
    if (auto err = validateTemplate(tmpl)) throw UserError(*err);
    const bool numbered = usesNumber(tmpl);

    std::set<FolderId> unique(folders.begin(), folders.end());
    for (const auto& folderId : unique) {
        if (!m.folder(folderId)) continue;
        const std::string folderPath = m.folderPath(folderId);
        auto clips = numberingOrder(m, m.clipsInFolder(folderId));

        std::set<std::string> reserved; // lowercased titles in use

        auto taken = [&](const std::string& title, const std::string& ext) {
            if (reserved.count(lowerTitleKey(title))) return true;
            if (m.childFolderNamed(folderId, title + ext)) return true;
            if (occupied && occupied(path::join(folderPath, title + ext))) return true;
            return false;
        };

        // Manually titled clips keep their names; resolve duplicates among them.
        for (const auto& cid : clips) {
            const Clip& c = *m.clip(cid);
            if (!c.titleOverride) continue;
            std::string title = c.title;
            for (int n = 2; reserved.count(lowerTitleKey(title)); ++n) title = c.title + " (" + std::to_string(n) + ")";
            reserved.insert(lowerTitleKey(title));
            if (title != c.title) {
                Clip copy = c;
                copy.title = title;
                copy.updatedAt = nowUnixSeconds();
                m.putClip(copy);
            }
        }

        int counter = 1;
        for (const auto& cid : clips) {
            const Clip& c = *m.clip(cid);
            if (c.titleOverride) continue;
            std::string title;
            if (numbered) {
                while (true) {
                    title = render(tmpl, contextFor(m, cid, counter));
                    ++counter;
                    if (!taken(title, c.extension)) break;
                    if (counter > 1000000) throw UserError("Unable to find a free name for " + c.originalFilename);
                }
            } else {
                std::string base = render(tmpl, contextFor(m, cid, counter));
                title = base;
                for (int n = 2; taken(title, c.extension); ++n) title = base + "_" + std::to_string(n);
            }
            reserved.insert(lowerTitleKey(title));
            if (title != c.title) {
                Clip copy = c;
                copy.title = title;
                copy.updatedAt = nowUnixSeconds();
                m.putClip(copy);
            }
        }
    }
}

} // namespace vo
