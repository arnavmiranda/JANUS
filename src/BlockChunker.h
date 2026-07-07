#pragma once

#include <vector>
#include <cstdint>

struct FileBlock
{
    std::vector<uint8_t> bytes;

    size_t logicalOffset = 0;
};

class BlockChunker
{
public:
    static std::vector<FileBlock> split(
        const std::vector<uint8_t>& file);
};