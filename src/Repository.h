#pragma once

#include <memory>
#include <string>

class Database;
class BlockStore;

class Repository
{
public:

    explicit Repository(const std::string& root);

    ~Repository();

private:

    std::unique_ptr<Database> database_;

    std::unique_ptr<BlockStore> blockStore_;

    std::string root_;
};