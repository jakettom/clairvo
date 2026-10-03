#pragma once

#include <cstdint>
#include <string>

namespace vo {

// Stable database identities. IDs are random UUIDv4 strings and are never
// derived from filenames or file hashes (spec §7.1, invariants 2–3).
using Id = std::string;
using ProjectId = Id;
using FolderId = Id;
using ClipId = Id;
using TagId = Id;

Id generateId();

int64_t nowUnixSeconds();

} // namespace vo
