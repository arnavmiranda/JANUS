#include "Database.h"
#include "BlockStore.h"
#include <sstream>
#include <vector>
#include <ctime>

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
            filename TEXT UNIQUE,
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

std::string Database::commitSnapshot(BlockStore& cas, const std::string& parent_hash) {
    beginTransaction();
    std::string commit_hash;
    
    try {
        auto stmt = prepareStatement(
            "SELECT i.filename, i.size, i.mode, fb.block_hash "
            "FROM inodes i LEFT JOIN file_blocks fb ON i.id = fb.inode_id"
        );
        
        std::stringstream manifest;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            const char* filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
            int size = sqlite3_column_int(stmt.get(), 1);
            int mode = sqlite3_column_int(stmt.get(), 2);
            const char* block_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));
            
            std::string block_hash_str = block_hash ? block_hash : "EMPTY";
            
            manifest << "FILE|" << (filename ? filename : "") << "|" 
                     << size << "|" << mode << "|" << block_hash_str << "\n";
        }
        
        std::string manifest_str = manifest.str();
        std::vector<uint8_t> data(manifest_str.begin(), manifest_str.end());
        commit_hash = cas.writeBlock(data);
        
        auto insertStmt = prepareStatement(
            "INSERT INTO snapshots (timestamp, parent_hash, commit_hash) VALUES (?, ?, ?)"
        );
        sqlite3_bind_int64(insertStmt.get(), 1, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_bind_text(insertStmt.get(), 2, parent_hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt.get(), 3, commit_hash.c_str(), -1, SQLITE_STATIC);
        
        if (sqlite3_step(insertStmt.get()) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db.get());
            throw std::runtime_error("Failed to insert snapshot record: " + err);
        }
        
        commitTransaction();
    } catch (...) {
        rollbackTransaction();
        throw;
    }
    
    return commit_hash;
}

std::string Database::getLatestSnapshotHash() {
    auto stmt = prepareStatement("SELECT commit_hash FROM snapshots ORDER BY id DESC LIMIT 1");
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        return hash ? std::string(hash) : "";
    }
    return "";
}

void Database::checkoutSnapshot(BlockStore& cas, const std::string& commit_hash) {
    std::vector<uint8_t> data = cas.readBlock(commit_hash);
    std::string manifest(data.begin(), data.end());

    beginTransaction();
    try {
        executeQuery("DELETE FROM file_blocks;");
        executeQuery("DELETE FROM inodes;");

        std::istringstream iss(manifest);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            std::vector<std::string> tokens;
            std::string token;
            std::istringstream line_stream(line);
            while (std::getline(line_stream, token, '|')) {
                tokens.push_back(token);
            }

            if (tokens.size() != 5 || tokens[0] != "FILE") {
                throw std::runtime_error("Malformed manifest line: " + line);
            }

            std::string filename = tokens[1];
            int size = std::stoi(tokens[2]);
            int mode = std::stoi(tokens[3]);
            std::string block_hash = tokens[4];

            auto insertInode = prepareStatement(
                "INSERT INTO inodes (filename, mode, size, mtime) VALUES (?, ?, ?, strftime('%s','now'))"
            );
            sqlite3_bind_text(insertInode.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertInode.get(), 2, mode);
            sqlite3_bind_int(insertInode.get(), 3, size);

            if (sqlite3_step(insertInode.get()) != SQLITE_DONE) {
                const char* err = sqlite3_errmsg(db.get());
                throw std::runtime_error(std::string("Failed to insert inode: ") + (err ? err : ""));
            }

            sqlite3_int64 inode_id = sqlite3_last_insert_rowid(db.get());

            if (block_hash != "EMPTY") {
                auto insertBlock = prepareStatement(
                    "INSERT INTO file_blocks (inode_id, block_index, block_hash) VALUES (?, 0, ?)"
                );
                sqlite3_bind_int64(insertBlock.get(), 1, inode_id);
                sqlite3_bind_text(insertBlock.get(), 2, block_hash.c_str(), -1, SQLITE_STATIC);

                if (sqlite3_step(insertBlock.get()) != SQLITE_DONE) {
                    const char* err = sqlite3_errmsg(db.get());
                    throw std::runtime_error(std::string("Failed to insert file block mapping: ") + (err ? err : ""));
                }
                
                auto blockStmt = prepareStatement("INSERT OR IGNORE INTO blocks (hash, size, refcount) VALUES (?, ?, 1)");
                sqlite3_bind_text(blockStmt.get(), 1, block_hash.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(blockStmt.get(), 2, size);
                sqlite3_step(blockStmt.get());
            }
        }
        commitTransaction();
    } catch (...) {
        rollbackTransaction();
        throw;
    }
}

bool Database::removeInode(const std::string& filename) {
    bool removed = false;
    beginTransaction();
    try {
        auto stmtId = prepareStatement("SELECT id FROM inodes WHERE filename = ?");
        sqlite3_bind_text(stmtId.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
        
        if (sqlite3_step(stmtId.get()) == SQLITE_ROW) {
            sqlite3_int64 inode_id = sqlite3_column_int64(stmtId.get(), 0);
            
            auto delFb = prepareStatement("DELETE FROM file_blocks WHERE inode_id = ?");
            sqlite3_bind_int64(delFb.get(), 1, inode_id);
            if (sqlite3_step(delFb.get()) != SQLITE_DONE) {
                const char* err = sqlite3_errmsg(db.get());
                throw std::runtime_error(std::string("Failed to delete file blocks: ") + (err ? err : ""));
            }
            
            auto delIn = prepareStatement("DELETE FROM inodes WHERE id = ?");
            sqlite3_bind_int64(delIn.get(), 1, inode_id);
            if (sqlite3_step(delIn.get()) != SQLITE_DONE) {
                const char* err = sqlite3_errmsg(db.get());
                throw std::runtime_error(std::string("Failed to delete inode: ") + (err ? err : ""));
            }
            removed = true;
        }
        commitTransaction();
    } catch (...) {
        rollbackTransaction();
        throw;
    }
    return removed;
}

