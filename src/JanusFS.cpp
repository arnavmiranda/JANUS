#include "JanusFS.h"
#include <string.h>
#include <errno.h>
#include <vector>
#include <iostream>
#include <stdexcept>

JanusFS::JanusFS(Database& db, BlockStore& cas) : db(db), cas(cas) {}
JanusFS::~JanusFS() {}

int JanusFS::getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void) fi;
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    std::string filename = path + 1; // Strip leading slash

    try {
        auto stmt = db.prepareStatement("SELECT mode, size, mtime FROM inodes WHERE filename = ?");
        sqlite3_bind_text(stmt.get(), 1, filename.c_str(), -1, SQLITE_STATIC);

        int rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) {
            stbuf->st_mode = sqlite3_column_int(stmt.get(), 0);
            stbuf->st_size = sqlite3_column_int(stmt.get(), 1);
            stbuf->st_mtime = sqlite3_column_int(stmt.get(), 2);
            stbuf->st_nlink = 1;
            return 0;
        } else if (rc != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(stmt.get()));
            throw std::runtime_error(std::string("SELECT mode, size, mtime FROM inodes failed: ") + err);
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        return -EIO;
    }

    return -ENOENT;
}

int JanusFS::readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));

    try {
        auto stmt = db.prepareStatement("SELECT filename FROM inodes");
        int rc;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
            filler(buf, name, NULL, 0, static_cast<fuse_fill_dir_flags>(0));
        }
        if (rc != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(stmt.get()));
            throw std::runtime_error(std::string("SELECT filename FROM inodes failed: ") + err);
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        return -EIO;
    }

    return 0;
}

int JanusFS::create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void) fi;
    std::string filename = path + 1;

    try {
        db.beginTransaction();
        auto stmt = db.prepareStatement("INSERT INTO inodes (filename, mode, size, mtime) VALUES (?, ?, 0, strftime('%s','now'))");
        sqlite3_bind_text(stmt.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
        
        mode_t final_mode = S_IFREG | (mode & 0777);
        if ((mode & S_IFMT) == 0) {
            final_mode = S_IFREG | 0644;
        }
        sqlite3_bind_int(stmt.get(), 2, final_mode);
        
        int rc = sqlite3_step(stmt.get());
        if (rc != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(stmt.get()));
            throw std::runtime_error(std::string("INSERT INTO inodes failed: ") + err);
        }
        db.commitTransaction();
    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        try { db.rollbackTransaction(); } catch(...) {}
        return -EIO;
    }

    return 0;
}

int JanusFS::write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void)fi;

    std::string filename = path + 1;

    try
    {
        db.writeFile(
            filename,
            buf,
            size,
            offset,
            cas);

        return static_cast<int>(size);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr
            << "[RUNTIME] "
            << e.what()
            << std::endl;

        return -EIO;
    }
    catch (const std::exception& e)
    {
        std::cerr << "\n[FUSE FATAL ERROR in "
                  << __func__
                  << "]: "
                  << e.what()
                  << "\n\n";

        return -EIO;
    }
}
int JanusFS::read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    (void)fi;

    std::string filename = path + 1;

    try
    {
        auto data =
            db.readFile(
                filename,
                cas);

        if (offset >= static_cast<off_t>(data.size()))
            return 0;

        size_t bytesToRead =
            std::min(
                size,
                data.size() -
                static_cast<size_t>(offset));

        memcpy(
            buf,
            data.data() + offset,
            bytesToRead);

        return static_cast<int>(bytesToRead);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr
            << "[RUNTIME] "
            << e.what()
            << std::endl;

        return -EIO;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "\n[FUSE FATAL ERROR in "
            << __func__
            << "]: "
            << e.what()
            << "\n\n";

        return -EIO;
    }
}

int JanusFS::unlink(const char* path)
{
    std::string filename = path + 1;

    try
    {
        db.unlinkFile(
            filename,
            cas);

        return 0;
    }
    catch (const std::runtime_error& e)
    {
        std::cerr
            << "[RUNTIME] "
            << e.what()
            << std::endl;

        return -EIO;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "\n[FUSE FATAL ERROR in "
            << __func__
            << "]: "
            << e.what()
            << "\n\n";

        return -EIO;
    }
}
int JanusFS::truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    (void) fi;

    std::string filename = path + 1;

    try
    {
        db.truncateFile(
            filename,
            static_cast<size_t>(size),
            cas);

        return 0;
    }
    catch (const std::runtime_error& e)
    {
        std::cerr
            << "[RUNTIME] "
            << e.what()
            << std::endl;

        return -EIO;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        return -EIO;
    }
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

int JanusFS::wrap_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    return get_instance()->create(path, mode, fi);
}

int JanusFS::wrap_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    return get_instance()->write(path, buf, size, offset, fi);
}

int JanusFS::wrap_unlink(const char* path) {
    return get_instance()->unlink(path);
}

int JanusFS::wrap_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    return get_instance()->truncate(path, size, fi);
}
