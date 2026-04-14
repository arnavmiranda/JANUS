#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <iostream>
#include <filesystem>
#include "JanusFS.h"
#include "Database.h"
#include "BlockStore.h"

int main(int argc, char* argv[]) {
    try {
        std::string absolute_path = std::filesystem::current_path().string();
        
        Database db(absolute_path + "/janus_meta.db");
        std::cout << "[JANUS] SQLite Metadata Engine Initialized." << std::endl;
        db.initSchema();

        BlockStore cas(absolute_path);
        std::cout << "[JANUS] CAS Block Storage Engine Initialized." << std::endl;

        JanusFS fs(db, cas);

        struct fuse_operations janus_oper = {};
        janus_oper.getattr = JanusFS::wrap_getattr;
        janus_oper.readdir = JanusFS::wrap_readdir;
        janus_oper.read = JanusFS::wrap_read;
        janus_oper.create = JanusFS::wrap_create;
        janus_oper.write = JanusFS::wrap_write;

        struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

        int ret = fuse_main(args.argc, args.argv, &janus_oper, &fs);
        
        fuse_opt_free_args(&args);
        return ret;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Database failed: " << e.what() << std::endl;
        return 1;
    }
}
