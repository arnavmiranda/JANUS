#pragma once

#include <sqlite3.h>
#include <string>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

struct SQLiteDeleter {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};

struct SQLiteStmtDeleter {
    void operator()(sqlite3_stmt* stmt) const {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }
};

using DatabasePtr = std::unique_ptr<sqlite3, SQLiteDeleter>;
using StatementPtr = std::unique_ptr<sqlite3_stmt, SQLiteStmtDeleter>;

class BlockStore;

class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database() = default;

    void initSchema();
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

    void executeQuery(const std::string& query);
    StatementPtr prepareStatement(const std::string& query);

    std::string commitSnapshot(BlockStore& cas, const std::string& message = "", const std::string& parent_hash = "");
    std::string getLatestSnapshotHash();
    void checkoutSnapshot(BlockStore& cas, const std::string& commit_hash);
    bool removeInode(const std::string& filename);
    void diffSnapshots(BlockStore& cas, const std::string& hash1, const std::string& hash2);
    std::vector<std::pair<std::string, std::string>> getSnapshotHistory();
    void printStats(bool asJson);

private:
    DatabasePtr db;

    // .janusignore helpers
    std::vector<std::string> getIgnoreList(BlockStore& cas);
    bool isIgnored(const std::string& filename, const std::vector<std::string>& ignoreList);
};
