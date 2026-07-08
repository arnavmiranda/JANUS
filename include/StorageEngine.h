#pragma once

#include "storage/FileLayout.h"

#include <cstdint>
#include <vector>

class BlockStore;

class StorageEngine
{
public:
    explicit StorageEngine(BlockStore& objectStore);

    FileLayout createLayout(const std::vector<uint8_t>& bytes);

    std::vector<uint8_t> loadLayout(const FileLayout& layout);

    void deleteOrphans(const std::vector<std::string>& hashes);

private:
    BlockStore& objectStore_;
};