#include "../include/StorageEngine.h"
#include "../include/BlockChunker.h"
#include "../include/FileAssembler.h"
#include "../include/BlockStore.h"

StorageEngine::StorageEngine(BlockStore& objectStore)
    : objectStore_(objectStore)
{
}


FileLayout StorageEngine::createLayout(const std::vector<uint8_t>& bytes)
{
    FileLayout layout;
    layout.logicalSize = bytes.size();

    auto blocks = BlockChunker::split(bytes);

    layout.blocks.reserve(blocks.size());

    for (const auto& block : blocks)
    {
        LayoutBlock layoutBlock;

        layoutBlock.hash = objectStore_.writeBlock(block.bytes);
        layoutBlock.size = static_cast<uint32_t>(block.bytes.size());

        layout.blocks.push_back(std::move(layoutBlock));
    }

    return layout;
}

std::vector<uint8_t> StorageEngine::loadLayout(const FileLayout& layout)
{
    std::vector<FileBlock> blocks;
    blocks.reserve(layout.blocks.size());

    for (const auto& layoutBlock : layout.blocks)
    {
        FileBlock block;
        block.bytes = objectStore_.readBlock(layoutBlock.hash);

        blocks.push_back(std::move(block));
    }

    return FileAssembler::assemble(blocks, layout.logicalSize);
}

void StorageEngine::deleteOrphans(const std::vector<std::string>& hashes)
{
    for (const auto& hash : hashes)
    {
        objectStore_.deleteBlock(hash);
    }
}