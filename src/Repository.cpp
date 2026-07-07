#include "../include/Repository.h"

Repository::Repository(const std::string& repositoryRoot)
    : metadata_(repositoryRoot + "/janus_meta.db"),
      objectStore_(repositoryRoot)
{
    metadata_.initSchema();
}


void Repository::write(
    const std::string& filename,
    const char* buffer,
    size_t size,
    off_t offset)
{
    metadata_.writeFile(
        filename,
        buffer,
        size,
        offset,
        objectStore_);
}


std::vector<uint8_t> Repository::read(
    const std::string& filename)
{
    return metadata_.readFile(
        filename,
        objectStore_);
}

void Repository::unlink(
    const std::string& filename)
{
    metadata_.unlinkFile(
        filename,
        objectStore_);
}

void Repository::truncate(
    const std::string& filename,
    size_t newSize)
{
    metadata_.truncateFile(
        filename,
        newSize,
        objectStore_);
}


std::string Repository::commitSnapshot(
    const std::string& message)
{
    return metadata_.commitSnapshot(
        objectStore_,
        message);
}
void Repository::checkoutSnapshot(
    const std::string& hash)
{
    metadata_.checkoutSnapshot(
        objectStore_,
        hash);
}
void Repository::diffSnapshots(
    const std::string& hash1,
    const std::string& hash2)
{
    metadata_.diffSnapshots(
        objectStore_,
        hash1,
        hash2);
}

std::vector<std::pair<std::string, std::string>>
Repository::getSnapshotHistory()
{
    return metadata_.getSnapshotHistory();
}
void Repository::printStats(bool asJson)
{
    metadata_.printStats(asJson);
}


StatementPtr Repository::prepareStatement(
    const std::string& query)
{
    return metadata_.prepareStatement(query);
}

void Repository::beginTransaction()
{
    metadata_.beginTransaction();
}

void Repository::commitTransaction()
{
    metadata_.commitTransaction();
}

void Repository::rollbackTransaction()
{
    metadata_.rollbackTransaction();
}