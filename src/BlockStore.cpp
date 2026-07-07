#include "../include/BlockStore.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <random>

BlockStore::BlockStore(const std::string& base_dir) {
    blocks_dir = std::filesystem::path(base_dir) / ".janus" / "blocks";
    std::filesystem::create_directories(blocks_dir);
}

std::string BlockStore::calculateHash(const std::vector<uint8_t>& data) const {
    EvpMdCtxPtr ctx(EVP_MD_CTX_new());
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1) {
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int lengthOfHash = 0;

    if (EVP_DigestFinal_ex(ctx.get(), hash, &lengthOfHash) != 1) {
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    std::stringstream ss;
    for (unsigned int i = 0; i < lengthOfHash; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::string BlockStore::writeBlock(const std::vector<uint8_t>& data) {
    std::string hash_str = calculateHash(data);
    std::filesystem::path target_path = blocks_dir / hash_str;

    if (std::filesystem::exists(target_path)) {
        return hash_str; // Dedup
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    std::filesystem::path tmp_path = blocks_dir / (hash_str + ".tmp." + std::to_string(dis(gen)));

    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            throw std::runtime_error("Failed to open temp file for block write");
        }
        
        if (!data.empty()) {
            ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
        
        if (!ofs.good()) {
            std::filesystem::remove(tmp_path);
            throw std::runtime_error("Failed to write to temp file");
        }
    }

    std::filesystem::rename(tmp_path, target_path);

    return hash_str;
}

std::vector<uint8_t> BlockStore::readBlock(const std::string& hash) {
    std::filesystem::path target_path = blocks_dir / hash;

    if (!std::filesystem::exists(target_path)) {
        throw std::runtime_error("Block not found: " + hash);
    }

    std::ifstream ifs(target_path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        throw std::runtime_error("Failed to open block file for reading: " + hash);
    }

    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer;
    if (size > 0) {
        buffer.resize(size);
        if (!ifs.read(reinterpret_cast<char*>(buffer.data()), size)) {
            throw std::runtime_error("Failed to read block data: " + hash);
        }
    }

    return buffer;
}

void BlockStore::deleteBlock(const std::string& hash)
{
    std::filesystem::path target = blocks_dir / hash;

    std::error_code ec;
    std::filesystem::remove(target, ec);

    if (ec)
    {
        throw std::runtime_error(
            "Failed to delete CAS block: " +
            hash +
            " : " +
            ec.message());
    }
}
