#include "app/application/SearchService.h"

#include <algorithm>

#include "core/util/StringUtil.h"

namespace vo {

SearchResult search(const ProjectModel& m, const std::string& query) {
    SearchResult r;
    std::vector<std::string> terms;
    for (auto& t : split(toLower(query), ' '))
        if (!trim(t).empty()) terms.push_back(trim(t));
    if (terms.empty()) return r;

    auto matchesAll = [&](const std::string& haystack) {
        return std::all_of(terms.begin(), terms.end(),
                           [&](const std::string& t) { return haystack.find(t) != std::string::npos; });
    };

    for (const auto& [id, f] : m.folders()) {
        if (f.parentId.empty()) continue;
        if (matchesAll(toLower(f.name))) r.folders.push_back(id);
    }
    for (const auto& [id, c] : m.clips()) {
        std::string hay = c.title + c.extension + "\n" + c.originalFilename + "\n" + m.folderPath(c.folderId) + "\n";
        for (const auto& tagId : m.clipTags(id))
            if (const Tag* t = m.tag(tagId)) hay += t->name + "\n";
        if (auto md = m.metadata(id)) {
            for (const auto& [cat, value] : md->normalized) hay += displayValue(cat, value) + "\n" + value + "\n";
        }
        hay += c.status == ClipStatus::Missing ? "missing\n" : "";
        if (matchesAll(toLower(hay))) r.clips.push_back(id);
    }
    std::sort(r.folders.begin(), r.folders.end());
    std::sort(r.clips.begin(), r.clips.end());
    return r;
}

} // namespace vo
