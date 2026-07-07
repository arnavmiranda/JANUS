#include "FileAssembler.h"

std::vector<uint8_t>
FileAssembler::assemble(
    const std::vector<FileBlock>& blocks,
    size_t logicalSize)
{
    std::vector<uint8_t> result;

    result.reserve(logicalSize);

    for (const auto& block : blocks)
    {
        result.insert(
            result.end(),
            block.bytes.begin(),
            block.bytes.end());
    }

    result.resize(logicalSize);

    return result;
}