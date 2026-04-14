#include "JanusFS.h"
#include <string.h>
#include <errno.h>

JanusFS::JanusFS() : hello_str("Hello from JANUS!\n"), hello_path("/hello.txt") {}
JanusFS::~JanusFS() {}

int JanusFS::getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    } else if (strcmp(path, hello_path.c_str()) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = hello_str.length();
        return 0;
    }

    return -ENOENT;
}

int JanusFS::readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    // We only have the root directory.
    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, hello_path.c_str() + 1, NULL, 0, static_cast<fuse_fill_dir_flags>(0)); // "+ 1" to remove leading slash

    return 0;
}

int JanusFS::read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;

    if (strcmp(path, hello_path.c_str()) != 0) {
        return -ENOENT;
    }

    size_t len = hello_str.length();
    if (static_cast<size_t>(offset) < len) {
        if (offset + size > len) {
            size = len - offset;
        }
        memcpy(buf, hello_str.c_str() + offset, size);
    } else {
        size = 0;
    }

    return size;
}

// Static Wrappers
static JanusFS* get_instance() {
    struct fuse_context* context = fuse_get_context();
    return static_cast<JanusFS*>(context->private_data);
}

int JanusFS::wrap_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    return get_instance()->getattr(path, stbuf, fi);
}

int JanusFS::wrap_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    return get_instance()->readdir(path, buf, filler, offset, fi, flags);
}

int JanusFS::wrap_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    return get_instance()->read(path, buf, size, offset, fi);
}
