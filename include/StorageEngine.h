#pragma once

#include "storage/FileLayout.h"

#include <vector>
#include <cstdint>

class BlockStore;

class StorageEngine
{
public:

    explicit StorageEngine(BlockStore& objectStore);

    FileLayout store(const std::vector<uint8_t>& bytes);

    std::vector<uint8_t> load(const FileLayout& layout);

    FileLayout createLayout(const std::vector<uint8_t>& bytes);

    std::vector<uint8_t> loadLayout(const FileLayout& layout);

private:

    BlockStore& objectStore_;
};