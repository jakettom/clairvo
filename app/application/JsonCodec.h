#pragma once

#include <nlohmann/json.hpp>

#include "app/application/ImportService.h"
#include "app/application/ProjectSession.h"
#include "app/application/SearchService.h"

namespace vo::codec {

using nlohmann::json;

// Serialization of application state for the UI bridge. The UI only ever sees
// these JSON documents, never internal C++ types (roadmap §35).
json snapshot(const ProjectSession& session);
json clipDetails(const ProjectModel& m, const ClipId& id);
json changes(const ProjectModel& m, const ChangeSet& cs);
json preflight(const PreflightReport& r);
json applyOperation(const ApplyOperationResult& op);
json applyResult(const ApplyResult& r);
json refreshResult(const RefreshResult& r);
json searchResult(const SearchResult& r);
json categoryStats(const ProjectModel& m);

std::vector<MetadataCategory> criteriaFromJson(const json& j);

} // namespace vo::codec
