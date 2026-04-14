#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <openssl/evp.h>

struct EVPDtxDeleter {
    void operator()(EVP_MD_CTX* ctx) const {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
    }
};

using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EVPDtxDeleter>;

class BlockStore {
public:
    explicit BlockStore(const std::string& base_dir);
    ~BlockStore() = default;

    std::string writeBlock(const std::vector<uint8_t>& data);
    std::vector<uint8_t> readBlock(const std::string& hash);

private:
    std::filesystem::path blocks_dir;
    std::string calculateHash(const std::vector<uint8_t>& data) const;
};
