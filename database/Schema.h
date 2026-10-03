#pragma once

#include "database/Sqlite.h"

namespace vo::db {

constexpr int kCurrentSchemaVersion = 1;

// Applies migrations v(n) -> v(n+1) up to kCurrentSchemaVersion (spec §55).
// Throws if the file was written by a newer, incompatible version.
void migrate(Database& db);

} // namespace vo::db
