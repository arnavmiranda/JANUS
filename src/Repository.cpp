#include "../include/Repository.h"

Repository::Repository(const std::string& repositoryRoot)
    : metadata_(repositoryRoot + "/janus_meta.db"),
      objectStore_(repositoryRoot),
      storage_(objectStore_)
{
    metadata_.initSchema();
}

void Repository::write(
    const std::string& filename,
    const char* buffer,
    size_t size,
    off_t offset)
{
    metadata_.beginTransaction();

    try
    {
        int inodeId =
            metadata_.getInodeId(filename);

        FileLayout layout =
            metadata_.getCurrentFileLayout(inodeId);

        std::vector<uint8_t> data =
            storage_.loadLayout(layout);

        const size_t requiredSize =
            static_cast<size_t>(offset) + size;

        if (data.size() < requiredSize)
        {
            data.resize(requiredSize, 0);
        }

        std::copy(
            buffer,
            buffer + size,
            data.begin() + offset);

        FileLayout newLayout =
            storage_.createLayout(data);

        metadata_.commitLayout(
            inodeId,
            newLayout,
            objectStore_);
    }
    catch (...)
    {
        metadata_.rollbackTransaction();
        throw;
    }
}

std::vector<uint8_t> Repository::read(
    const std::string& filename)
{
    const int inodeId =
        metadata_.getInodeId(filename);

    const FileLayout layout =
        metadata_.getCurrentFileLayout(inodeId);

    return storage_.loadLayout(layout);
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
    metadata_.beginTransaction();

    try
    {
        int inodeId =
            metadata_.getInodeId(filename);

        FileLayout layout =
            metadata_.getCurrentFileLayout(inodeId);

        auto data =
            storage_.loadLayout(layout);

        data.resize(newSize, 0);

        FileLayout newLayout =
            storage_.createLayout(data);

        metadata_.commitLayout(
            inodeId,
            newLayout,
            objectStore_);
    }
    catch (...)
    {
        metadata_.rollbackTransaction();
        throw;
    }
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