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

int JanusFS::write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;
    std::string filename = path + 1;

    try {
        db.beginTransaction();
        
        auto getInodeStmt = db.prepareStatement("SELECT id FROM inodes WHERE filename = ?");
        sqlite3_bind_text(getInodeStmt.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
        
        int rc = sqlite3_step(getInodeStmt.get());
        if (rc == SQLITE_DONE) {
            db.rollbackTransaction();
            return -ENOENT;
        } else if (rc != SQLITE_ROW) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(getInodeStmt.get()));
            throw std::runtime_error(std::string("SELECT id FROM inodes failed: ") + err);
        }
        int inode_id = sqlite3_column_int(getInodeStmt.get(), 0);

        std::vector<uint8_t> data(buf, buf + size);
        std::string hash = cas.writeBlock(data);

        auto blockStmt = db.prepareStatement("INSERT OR IGNORE INTO blocks (hash, size, refcount) VALUES (?, ?, 1)");
        sqlite3_bind_text(blockStmt.get(), 1, hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(blockStmt.get(), 2, size);
        
        if (sqlite3_step(blockStmt.get()) != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(blockStmt.get()));
            throw std::runtime_error(std::string("INSERT INTO blocks failed: ") + err);
        }

        auto cleanMappings = db.prepareStatement("DELETE FROM file_blocks WHERE inode_id = ?");
        sqlite3_bind_int(cleanMappings.get(), 1, inode_id);
        
        if (sqlite3_step(cleanMappings.get()) != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(cleanMappings.get()));
            throw std::runtime_error(std::string("DELETE FROM file_blocks failed: ") + err);
        }

        auto mappingStmt = db.prepareStatement("INSERT INTO file_blocks (inode_id, block_index, block_hash) VALUES (?, 0, ?)");
        sqlite3_bind_int(mappingStmt.get(), 1, inode_id);
        sqlite3_bind_text(mappingStmt.get(), 2, hash.c_str(), -1, SQLITE_STATIC);
        
        if (sqlite3_step(mappingStmt.get()) != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(mappingStmt.get()));
            throw std::runtime_error(std::string("INSERT INTO file_blocks failed: ") + err);
        }

        auto sizeStmt = db.prepareStatement("UPDATE inodes SET size = ?, mtime = strftime('%s','now') WHERE id = ?");
        sqlite3_bind_int(sizeStmt.get(), 1, size);
        sqlite3_bind_int(sizeStmt.get(), 2, inode_id);
        
        if (sqlite3_step(sizeStmt.get()) != SQLITE_DONE) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(sizeStmt.get()));
            throw std::runtime_error(std::string("UPDATE inodes failed: ") + err);
        }

        db.commitTransaction();
        return size;

    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        try { db.rollbackTransaction(); } catch(...) {}
        return -EIO;
    }
}

int JanusFS::read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;
    std::string filename = path + 1;

    try {
        auto getInodeStmt = db.prepareStatement("SELECT id FROM inodes WHERE filename = ?");
        sqlite3_bind_text(getInodeStmt.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
        
        int rc = sqlite3_step(getInodeStmt.get());
        if (rc == SQLITE_DONE) {
            return -ENOENT;
        } else if (rc != SQLITE_ROW) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(getInodeStmt.get()));
            throw std::runtime_error(std::string("SELECT id FROM inodes failed: ") + err);
        }
        int inode_id = sqlite3_column_int(getInodeStmt.get(), 0);

        auto mappingStmt = db.prepareStatement("SELECT block_hash FROM file_blocks WHERE inode_id = ? LIMIT 1");
        sqlite3_bind_int(mappingStmt.get(), 1, inode_id);

        int rc_map = sqlite3_step(mappingStmt.get());
        if (rc_map == SQLITE_DONE) {
            return 0; // Empty file
        } else if (rc_map != SQLITE_ROW) {
            const char* err = sqlite3_errmsg(sqlite3_db_handle(mappingStmt.get()));
            throw std::runtime_error(std::string("SELECT block_hash FROM file_blocks failed: ") + err);
        }
        
        const char* hash_cstr = reinterpret_cast<const char*>(sqlite3_column_text(mappingStmt.get(), 0));
        std::string hash(hash_cstr);

        std::vector<uint8_t> data = cas.readBlock(hash);
        
        if (offset < data.size()) {
            if (offset + size > data.size()) {
                size = data.size() - offset;
            }
            memcpy(buf, data.data() + offset, size);
            return size;
        } else {
            return 0;
        }

    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
        return -EIO;
    }
}

int JanusFS::unlink(const char* path) {
    std::string filename = path + 1;
    try {
        if (db.removeInode(filename)) {
            return 0;
        } else {
            return -ENOENT;
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[FUSE FATAL ERROR in " << __func__ << "]: " << e.what() << "\n\n";
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

