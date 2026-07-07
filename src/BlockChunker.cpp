#include "BlockChunker.h"

std::vector<FileBlock>
BlockChunker::split(
    const std::vector<uint8_t>& file)
{
    std::vector<FileBlock> blocks;

    size_t offset = 0;

    while (offset < file.size())
    {
        size_t chunkSize =
            std::min(BLOCK_SIZE,
                     file.size() - offset);

        FileBlock block;

        block.bytes.insert(
            block.bytes.end(),
            file.begin() + offset,
            file.begin() + offset + chunkSize);

        blocks.push_back(std::move(block));

        offset += chunkSize;
    }

    // Preserve empty files.
    if (blocks.empty())
    {
        blocks.emplace_back();
    }

    return blocks;
}