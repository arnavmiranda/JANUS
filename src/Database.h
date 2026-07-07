#pragma once

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


    //internal consistency verifier that validates the integirty of the cas metadata after every ownership transition
    //this is public so that we can later call this with writeFile, unlinkFile, checkout, tests, etc
    void verifyReferenceCounts();

    // File operations (storage engine API)
    void writeFile(
        const std::string& filename,
        const char* buffer,
        size_t size,
        off_t offset,
        BlockStore& cas);

    std::vector<uint8_t> readFile(
        const std::string& filename,
        BlockStore& cas);

    void unlinkFile(
        const std::string& filename,
        BlockStore& cas);

private:
    DatabasePtr db;

    //metadata
    int getInodeId(const std::string& filename);

    std::vector<std::string> getCurrentBlockHashes(int inodeId);

    void replaceFileMappings(
        int inodeId,
        const std::vector<std::string>& newBlockHashes);

    void updateInodeMetadata(
        int inodeId,
        size_t newSize);

    std::vector<uint8_t> loadFileContents(
        int inodeId,
        BlockStore& cas);

    std::vector<std::string> storeFileContents(
        const std::vector<uint8_t>& data,
        BlockStore& cas);

    //refcounting
    void insertBlockMetadata(
        const std::string& hash,
        size_t size);

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
