#include "Database.h"

Database::Database(const std::string& db_path) {
    sqlite3* raw_db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &raw_db);
    
    db = DatabasePtr(raw_db);

    if (rc != SQLITE_OK) {
        std::string err_msg = "Failed to open database: ";
        if (raw_db) {
            err_msg += sqlite3_errmsg(raw_db);
        } else {
            err_msg += "Unknown memory error";
        }
        throw std::runtime_error(err_msg);
    }

    sqlite3_busy_timeout(db.get(), 5000);

    executeQuery("PRAGMA journal_mode=WAL;");
    executeQuery("PRAGMA foreign_keys=ON;");
}

void Database::executeQuery(const std::string& query) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db.get(), query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string error = err_msg ? err_msg : "Unknown error";
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        throw std::runtime_error("SQL error: " + error);
    }
}

StatementPtr Database::prepareStatement(const std::string& query) {
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db.get(), query.c_str(), -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err_msg = sqlite3_errmsg(db.get());
        throw std::runtime_error("Failed to prepare statement: " + err_msg);
    }
    return StatementPtr(raw_stmt);
}

void Database::initSchema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS inodes (
            id INTEGER PRIMARY KEY,
            mode INTEGER,
            size INTEGER,
            mtime INTEGER
        );

        CREATE TABLE IF NOT EXISTS blocks (
            hash TEXT PRIMARY KEY,
            size INTEGER,
            refcount INTEGER
        );

        CREATE TABLE IF NOT EXISTS file_blocks (
            inode_id INTEGER,
            block_index INTEGER,
            block_hash TEXT,
            FOREIGN KEY(inode_id) REFERENCES inodes(id),
            FOREIGN KEY(block_hash) REFERENCES blocks(hash)
        );

        CREATE TABLE IF NOT EXISTS snapshots (
            id INTEGER PRIMARY KEY,
            timestamp INTEGER,
            parent_hash TEXT,
            commit_hash TEXT
        );
    )";
    executeQuery(schema);
}

void Database::beginTransaction() {
    executeQuery("BEGIN TRANSACTION;");
}

void Database::commitTransaction() {
    executeQuery("COMMIT;");
}

void Database::rollbackTransaction() {
    executeQuery("ROLLBACK;");
}
