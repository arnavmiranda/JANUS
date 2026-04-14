#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <iostream>
#include "JanusFS.h"

int main(int argc, char* argv[]) {
    JanusFS fs;

    struct fuse_operations janus_oper = {};
    janus_oper.getattr = JanusFS::wrap_getattr;
    janus_oper.readdir = JanusFS::wrap_readdir;
    janus_oper.read = JanusFS::wrap_read;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    int ret = fuse_main(args.argc, args.argv, &janus_oper, &fs);
    
    fuse_opt_free_args(&args);
    return ret;
}
