#ifndef JANUSFS_H
#define JANUSFS_H

#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string>
#include "Repository.h"

class JanusFS {
public:
    explicit JanusFS(Repository& repository);
    ~JanusFS();

    int getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
    int readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);
    int read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int create(const char* path, mode_t mode, struct fuse_file_info* fi);
    int write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    int unlink(const char* path);
    int truncate(const char *path, off_t size, struct fuse_file_info *fi);

    static int wrap_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
    static int wrap_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);
    static int wrap_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int wrap_create(const char* path, mode_t mode, struct fuse_file_info* fi);
    static int wrap_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int wrap_unlink(const char* path);
    static int wrap_truncate(const char *path, off_t size, struct fuse_file_info *fi);

private:
    Repository& repository;
};

#endif // JANUSFS_H
