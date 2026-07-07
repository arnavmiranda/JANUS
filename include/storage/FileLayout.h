#pragma once

#include <string>
#include <vector>

struct FileLayout
{
    size_t logicalSize = 0;

    std::vector<std::string> blockHashes;
};