#pragma once

#include <cstdint>
#include <vector>

constexpr size_t BLOCK_SIZE = 4096;

struct FileBlock
{
    std::vector<uint8_t> bytes;
};

class BlockChunker
{
public:
    static std::vector<FileBlock>
    split(const std::vector<uint8_t>& file);
};