#ifndef JANUSFS_H
#define JANUSFS_H

#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string>

class JanusFS {
public:
    JanusFS();
    ~JanusFS();

    int getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
    int readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);
    int read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);

    static int wrap_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
    static int wrap_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags);
    static int wrap_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);

private:
    std::string hello_str;
    std::string hello_path;
};

#endif // JANUSFS_H
