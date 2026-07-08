#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LayoutBlock
{
    std::string hash;
    uint32_t size;
};

struct FileLayout
{
    uint64_t logicalSize = 0;
    std::vector<LayoutBlock> blocks;
};