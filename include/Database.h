#pragma once

#include "../include/storage/FileLayout.h"
#include <optional>
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
    void diffSnapshots(BlockStore& cas, const std::string& hash1, const std::string& hash2);
    std::vector<std::pair<std::string, std::string>> getSnapshotHistory();
    void printStats(bool asJson);

    // void truncateFile(const std::string &filename, size_t newSize, BlockStore &cas);
    
    // internal consistency verifier that validates the integirty of the cas metadata after every ownership transition
    // this is public so that we can later call this with writeFile, unlinkFile, checkout, tests, etc
    void verifyReferenceCounts();

    // // File operations (storage engine API)
    // void writeFile(const std::string &filename, const char *buffer, size_t size, off_t offset, BlockStore &cas);

    // std::vector<uint8_t> readFile(const std::string &filename, BlockStore &cas);

    void unlinkFile(const std::string &filename, BlockStore &cas);

    //metadata
    int getInodeId(const std::string& filename);

    FileLayout getCurrentFileLayout(int inodeId);

    void replaceFileMappings(int inodeId, const FileLayout &layout);

    void updateInodeMetadata(int inodeId, size_t newSize);

    void Database::commitLayout(int inodeId, const FileLayout& newLayout, BlockStore& cas);

private:
    DatabasePtr db;

    std::vector<uint8_t> loadLayout(
        int inodeId,
        BlockStore& cas);


    //refcounting
    void insertBlockMetadata(
        const std::string& hash,
        size_t blockSize);

    void incrementRefcount(
        const std::string& hash);

    bool decrementRefcount(
        const std::string& hash);

    void deleteBlockMetadata(
        const std::string& hash);


    // .janusignore helpers
    std::vector<std::string> getIgnoreList(BlockStore& cas);
    bool isIgnored(const std::string& filename, const std::vector<std::string>& ignoreList);

    
};
