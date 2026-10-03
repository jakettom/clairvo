#include "database/Sqlite.h"

#include <sqlite3.h>

namespace vo::db {

namespace {
[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw SqliteError(what + ": " + (db ? sqlite3_errmsg(db) : "no database"));
}
} // namespace

Database::~Database() { close(); }

void Database::open(const std::string& path, bool create) {
    close();
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | (create ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unable to open";
        sqlite3_close(db_);
        db_ = nullptr;
        throw SqliteError("Cannot open project database: " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);
    // A rollback journal (not WAL) keeps the root free of persistent side files.
    exec("PRAGMA journal_mode = DELETE");
    exec("PRAGMA synchronous = FULL");
    exec("PRAGMA foreign_keys = ON");
}

void Database::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw SqliteError("SQL error: " + msg);
    }
}

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

int Database::userVersion() {
    auto st = prepare("PRAGMA user_version");
    st.step();
    return static_cast<int>(st.integer(0));
}

void Database::setUserVersion(int v) { exec("PRAGMA user_version = " + std::to_string(v)); }

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) fail(db, "Prepare failed (" + sql + ")");
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) { other.stmt_ = nullptr; }

Statement& Statement::bind(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        fail(db_, "Bind failed");
    return *this;
}

Statement& Statement::bind(int index, int64_t value) {
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) fail(db_, "Bind failed");
    return *this;
}

Statement& Statement::bind(int index, const std::optional<std::string>& value) {
    return value ? bind(index, *value) : bindNull(index);
}

Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) fail(db_, "Bind failed");
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    fail(db_, "Step failed");
}

void Statement::run() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::string Statement::text(int column) const {
    const unsigned char* t = sqlite3_column_text(stmt_, column);
    return t ? std::string(reinterpret_cast<const char*>(t), static_cast<size_t>(sqlite3_column_bytes(stmt_, column)))
             : std::string();
}

std::optional<std::string> Statement::optionalText(int column) const {
    if (sqlite3_column_type(stmt_, column) == SQLITE_NULL) return std::nullopt;
    return text(column);
}

int64_t Statement::integer(int column) const { return sqlite3_column_int64(stmt_, column); }

Transaction::Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
    if (!done_) {
        try {
            db_.exec("ROLLBACK");
        } catch (...) {
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

} // namespace vo::db
