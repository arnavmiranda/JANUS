#include "Repository.h"

Repository::Repository(const std::string& repositoryRoot)
    : metadata_(repositoryRoot + "/janus_meta.db"),
      objectStore_(repositoryRoot)
{
    metadata_.initSchema();
}

Database& Repository::metadata()
{
    return metadata_;
}

BlockStore& Repository::objectStore()
{
    return objectStore_;
}