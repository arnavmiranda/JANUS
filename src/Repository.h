#pragma once

#include "Database.h"
#include "BlockStore.h"

#include <string>

class Repository
{
public:

    explicit Repository(const std::string& repositoryRoot);

    ~Repository() = default;

    Database& metadata();
    BlockStore& objectStore();

private:

    Database metadata_;

    BlockStore objectStore_;
};