#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <iostream>
#include "JanusFS.h"
#include "Database.h" // Add this include

int main(int argc, char* argv[]) {
    try {
        Database db("janus_meta.db");
        std::cout << "[JANUS] SQLite Metadata Engine Initialized." << std::endl;

        JanusFS fs;

        struct fuse_operations janus_oper = {};
        janus_oper.getattr = JanusFS::wrap_getattr;
        janus_oper.readdir = JanusFS::wrap_readdir;
        janus_oper.read = JanusFS::wrap_read;

        struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

        int ret = fuse_main(args.argc, args.argv, &janus_oper, &fs);
        
        fuse_opt_free_args(&args);
        return ret;

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Database failed: " << e.what() << std::endl;
        return 1;
    }
}
