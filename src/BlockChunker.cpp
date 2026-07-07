#include "BlockChunker.h"

std::vector<FileBlock> BlockChunker::split(
    const std::vector<uint8_t>& file)
{
    FileBlock block;
    block.bytes = file;

    return { std::move(block) };
}