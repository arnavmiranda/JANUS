#pragma once

#include "BlockChunker.h"

#include <vector>
#include <cstdint>

class FileAssembler
{
public:

    static std::vector<uint8_t>
    assemble(const std::vector<FileBlock>& blocks,
             size_t logicalSize);
};