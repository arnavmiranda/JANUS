#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include "JanusFS.h"
#include "Database.h"
#include "BlockStore.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: janus <command> [args]\n";
        std::cerr << "Commands:\n";
        std::cerr << "  mount <dir>      Mount the Janus filesystem\n";
        std::cerr << "  commit           Snapshot the filesystem\n";
        std::cerr << "  log              View snapshot history\n";
        std::cerr << "  checkout <hash>  Restore a snapshot\n";
        return 1;
    }

    std::string command = argv[1];

    try {
        std::string absolute_path = std::filesystem::current_path().string();
        Database db(absolute_path + "/janus_meta.db");
        db.initSchema();
        BlockStore cas(absolute_path);

        if (command == "mount") {
            struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
            fuse_opt_add_arg(&args, argv[0]);
            fuse_opt_add_arg(&args, "-f");
            for (int i = 2; i < argc; ++i) {
                fuse_opt_add_arg(&args, argv[i]);
            }

            std::cout << "[JANUS] SQLite Metadata Engine Initialized." << std::endl;
            std::cout << "[JANUS] CAS Block Storage Engine Initialized." << std::endl;

            JanusFS fs(db, cas);

            struct fuse_operations janus_oper = {};
            janus_oper.getattr = JanusFS::wrap_getattr;
            janus_oper.readdir = JanusFS::wrap_readdir;
            janus_oper.read = JanusFS::wrap_read;
            janus_oper.create = JanusFS::wrap_create;
            janus_oper.write = JanusFS::wrap_write;
            janus_oper.unlink = JanusFS::wrap_unlink;
            
            std::cout << "[JANUS] FUSE Daemon Starting..." << std::endl;
            int ret = fuse_main(args.argc, args.argv, &janus_oper, &fs);
            fuse_opt_free_args(&args);
            return ret;

        } else if (command == "commit") {
            std::string hash = db.commitSnapshot(cas);
            std::cout << "Committed snapshot: " << hash << std::endl;
            return 0;

        } else if (command == "log") {
            auto stmt = db.prepareStatement("SELECT id, timestamp, commit_hash FROM snapshots ORDER BY id DESC");
            while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
                int id = sqlite3_column_int(stmt.get(), 0);
                int timestamp = sqlite3_column_int(stmt.get(), 1);
                const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
                std::cout << "Snapshot ID: " << id << " | Time: " << timestamp << " | Hash: " << (hash ? hash : "NULL") << "\n";
            }
            return 0;

        } else if (command == "checkout") {
            if (argc < 3) {
                std::cerr << "Usage: janus checkout <hash>\n";
                return 1;
            }
            std::string hash = argv[2];
            db.checkoutSnapshot(cas, hash);
            std::cout << "Successfully checked out snapshot: " << hash << std::endl;
            return 0;

        } else {
            std::cerr << "Unknown command: " << command << "\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] System failed: " << e.what() << std::endl;
        return 1;
    }
}
