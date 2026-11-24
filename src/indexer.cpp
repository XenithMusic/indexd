#include <cstddef>
#include <stdio.h>
#include <limits.h>
#include <sqlite3.h>
namespace Index {
    sqlite3* db = nullptr;
    void quit() {
        sqlite3_close(db);
    }
    int init() {
        int result = sqlite3_open("index.db",&db);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        result = sqlite3_exec(db, R"(
            CREATE TABLE IF NOT EXISTS files (
                path TEXT,
                size INTEGER,
                created REAL,
                modified REAL,
                isdir INTEGER,
                UNIQUE(path)
            );
        )", nullptr, nullptr, nullptr);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        result = sqlite3_exec(db,R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS files_fts
            USING fts5(path);)",nullptr,nullptr,nullptr);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        result = sqlite3_exec(db,R"(
            CREATE TRIGGER IF NOT EXISTS files_ai
            AFTER INSERT ON files BEGIN
                INSERT INTO files_fts(rowid, path)
                VALUES (new.id, new.path);
            END;)",nullptr,nullptr,nullptr);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        result = sqlite3_exec(db,R"(
            CREATE TRIGGER IF NOT EXISTS files_ad
            AFTER DELETE ON files BEGIN
                DELETE FROM files_fts WHERE rowid = old.id;
            END;)",nullptr,nullptr,nullptr);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        result = sqlite3_exec(db,R"(
            CREATE TRIGGER IF NOT EXISTS files_au
            AFTER UPDATE ON files BEGIN
                DELETE FROM files_fts WHERE rowid = old.id;
                INSERT INTO files_fts(rowid, path)
                VALUES (new.id, new.path);
            END;)",nullptr,nullptr,nullptr);
        if (result != SQLITE_OK) {
            quit();
            return result;
        }
        return result;
    }
    int add(const char* path) {
        const char* sql = R"(
INSERT OR IGNORE INTO files(path)
VALUES (?);
        )";
        char* error = nullptr;
        sqlite3_stmt* stmt;
        int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (result != SQLITE_OK) {
            if (stmt != nullptr) sqlite3_finalize(stmt);
            return result;
        }
        sqlite3_bind_text(stmt, 1, path, sizeof(path),SQLITE_TRANSIENT);
        result = sqlite3_step(stmt);
        if (result != SQLITE_OK) {
            if (stmt != nullptr) sqlite3_finalize(stmt);
            return result;
        }
        return result;
    }
    int remove(const char* path) {
        const char* sql = R"(
DELETE FROM files
WHERE path=?
        )";
        char* error = nullptr;
        sqlite3_stmt* stmt;
        int result = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (result != SQLITE_OK) {
            if (stmt != nullptr) sqlite3_finalize(stmt);
            return result;
        }
        sqlite3_bind_text(stmt, 1, path, sizeof(path),SQLITE_TRANSIENT);
        result = sqlite3_step(stmt);
        if (result != SQLITE_OK) {
            if (stmt != nullptr) sqlite3_finalize(stmt);
            return result;
        }
        return result;
    }
}