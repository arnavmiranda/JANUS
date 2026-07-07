#pragma once

#include "Database.h"
#include "BlockStore.h"

#include <string>

class Repository
{
public:

    explicit Repository(const std::string& repositoryRoot);

    ~Repository() = default;

    void write(
        const std::string& filename,
        const char* buffer,
        size_t size,
        off_t offset);

    std::vector<uint8_t> read(
        const std::string& filename);

    void truncate(
        const std::string& filename,
        size_t newSize);

    void unlink(
        const std::string& filename);

    // Snapshot API

    std::string commitSnapshot(
        const std::string& message = "");

    void checkoutSnapshot(
        const std::string& hash);

    void diffSnapshots(
        const std::string& hash1,
        const std::string& hash2);

    std::vector<std::pair<std::string, std::string>>
    getSnapshotHistory();

    void printStats(
        bool asJson);

    // Metadata API (temporary)

    StatementPtr prepareStatement(
        const std::string& query);

    void beginTransaction();

    void commitTransaction();

    void rollbackTransaction();


private:

    Database metadata_;

    BlockStore objectStore_;
};