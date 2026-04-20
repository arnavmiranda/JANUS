#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include "JanusFS.h"
#include "Database.h"
#include "BlockStore.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: janus <command> [args]\n";
        std::cerr << "Commands:\n";
        std::cerr << "  mount <dir>              Mount the Janus filesystem\n";
        std::cerr << "  commit [-m \"message\"]   Snapshot the filesystem\n";
        std::cerr << "  log                      Browse history interactively\n";
        std::cerr << "  checkout <hash>          Restore a snapshot\n";
        std::cerr << "  diff <hash1> <hash2>     Compare two snapshots\n";
        std::cerr << "  stats [--json]           Show filesystem statistics\n";
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
            std::string message = "Auto-commit";
            if (argc >= 4 && std::string(argv[2]) == "-m") {
                message = argv[3];
            }
            std::string hash = db.commitSnapshot(cas, message);
            std::cout << "Committed snapshot: " << hash << "\n";
            if (!message.empty()) {
                std::cout << "Message: " << message << "\n";
            }
            return 0;

        } else if (command == "log") {
            auto history = db.getSnapshotHistory();

            if (history.empty()) {
                std::cout << "\033[33mNo snapshots found. Run 'janus commit' to create one.\033[0m\n";
                return 0;
            }

            // ── Header ──────────────────────────────────────────────────────
            std::cout << "\n\033[1;36m  JANUS — Snapshot History\033[0m\n";
            std::cout << "\033[90m  " << std::string(52, '-') << "\033[0m\n";

            // ── Numbered list ────────────────────────────────────────────────
            for (std::size_t i = 0; i < history.size(); ++i) {
                const auto& [hash, msg] = history[i];
                // Abbreviate hash to first 12 chars for readability
                std::string short_hash = hash.size() > 12 ? hash.substr(0, 12) + "..." : hash;

                std::cout << "  \033[1;33m[" << i << "]\033[0m "
                          << "\033[37m" << short_hash << "\033[0m"
                          << "  \033[90m|\033[0m  "
                          << "\033[32m" << msg << "\033[0m\n";
            }

            std::cout << "\033[90m  " << std::string(52, '-') << "\033[0m\n";

            // ── Interactive prompt ───────────────────────────────────────────
            std::cout << "\n\033[1mEnter snapshot number to checkout (or 'q' to quit): \033[0m";
            std::string input;
            std::getline(std::cin, input);

            if (input == "q" || input == "Q") {
                std::cout << "\033[90mExiting.\033[0m\n";
                return 0;
            }

            try {
                int idx = std::stoi(input);
                if (idx < 0 || static_cast<std::size_t>(idx) >= history.size()) {
                    std::cerr << "\033[31mError: index " << idx << " is out of range.\033[0m\n";
                    return 1;
                }
                const std::string& target_hash = history[static_cast<std::size_t>(idx)].first;
                std::cout << "\033[36mChecking out snapshot " << idx
                          << " (" << target_hash.substr(0, 12) << "...)\033[0m\n";
                db.checkoutSnapshot(cas, target_hash);
                std::cout << "\033[32m✔ Successfully restored snapshot [" << idx << "]\033[0m\n";
            } catch (const std::invalid_argument&) {
                std::cerr << "\033[31mError: '" << input << "' is not a valid number or 'q'.\033[0m\n";
                return 1;
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

        } else if (command == "diff") {
            if (argc != 4) {
                std::cerr << "Usage: janus diff <hash1> <hash2>\n";
                return 1;
            }
            db.diffSnapshots(cas, argv[2], argv[3]);
            return 0;

        } else if (command == "stats") {
            // Accept --json anywhere in the remaining argv
            bool asJson = false;
            for (int i = 2; i < argc; ++i) {
                if (std::string(argv[i]) == "--json") {
                    asJson = true;
                    break;
                }
            }
            db.printStats(asJson);
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
