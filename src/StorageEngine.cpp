#include "../include/StorageEngine.h"
#include "../include/BlockChunker.h"
#include "../include/FileAssembler.h"
#include "../include/BlockStore.h"

StorageEngine::StorageEngine(BlockStore& objectStore)
    : objectStore_(objectStore)
{
}

FileLayout StorageEngine::store(const std::vector<uint8_t>& bytes)
{
    FileLayout layout;

    layout.logicalSize = bytes.size();

    auto blocks =
        BlockChunker::split(bytes);

    for (const auto& block : blocks)
    {
        layout.blockHashes.push_back(
            objectStore_.writeBlock(
                block.bytes));
    }

    return layout;
}

std::vector<uint8_t> StorageEngine::load(const FileLayout& layout)
{
    std::vector<FileBlock> blocks;

    blocks.reserve(layout.blockHashes.size());

    for (const auto& hash : layout.blockHashes)
    {
        FileBlock block;

        block.bytes =
            objectStore_.readBlock(hash);

        blocks.push_back(
            std::move(block));
    }

    return FileAssembler::assemble(
        blocks,
        layout.logicalSize);
}