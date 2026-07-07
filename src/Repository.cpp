#include "Repository.h"

#include "Database.h"
#include "BlockStore.h"

#include <filesystem>

Repository::Repository(const std::string& root)
    : root_(root)
{
    std::filesystem::create_directories(root_);

    database_ =
        std::make_unique<Database>(
            root_ + "/janus.db");

    blockStore_ =
        std::make_unique<BlockStore>(
            root_ + "/objects");

    database_->initSchema();
}

Repository::~Repository() = default;