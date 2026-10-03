#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace vo::db {

struct SqliteError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Statement;

// Thin RAII wrapper so SQL stays inside the database layer.
class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void open(const std::string& path, bool create);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);
    int64_t lastInsertRowId() const;
    int userVersion();
    void setUserVersion(int v);

    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, const std::string& value);
    Statement& bind(int index, int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<int64_t>(value)); }
    Statement& bind(int index, bool value) { return bind(index, static_cast<int64_t>(value ? 1 : 0)); }
    Statement& bind(int index, const std::optional<std::string>& value);
    Statement& bindNull(int index);

    bool step(); // true while a row is available
    void run();  // step to completion
    void reset();

    std::string text(int column) const;
    std::optional<std::string> optionalText(int column) const;
    int64_t integer(int column) const;

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

// BEGIN IMMEDIATE ... COMMIT; rolls back if destroyed without commit().
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    void commit();

private:
    Database& db_;
    bool done_ = false;
};

} // namespace vo::db
