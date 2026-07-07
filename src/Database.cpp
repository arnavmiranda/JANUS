#include "Database.h"
#include "BlockStore.h"
#include "BlockChunker.h"
#include <sstream>
#include <vector>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <unordered_map>

Database::Database(const std::string& db_path) {
    sqlite3* raw_db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &raw_db);
    
    db = DatabasePtr(raw_db);

    if (rc != SQLITE_OK) {
        std::string err_msg = "Failed to open database: ";
        if (raw_db) {
            err_msg += sqlite3_errmsg(raw_db);
        } else {
            err_msg += "Unknown memory error";
        }
        throw std::runtime_error(err_msg);
    }

    sqlite3_busy_timeout(db.get(), 5000);

    executeQuery("PRAGMA journal_mode=WAL;");
    executeQuery("PRAGMA foreign_keys=ON;");
}

void Database::executeQuery(const std::string& query) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db.get(), query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string error = err_msg ? err_msg : "Unknown error";
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        throw std::runtime_error("SQL error: " + error);
    }
}

StatementPtr Database::prepareStatement(const std::string& query) {
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db.get(), query.c_str(), -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err_msg = sqlite3_errmsg(db.get());
        throw std::runtime_error("Failed to prepare statement: " + err_msg);
    }
    return StatementPtr(raw_stmt);
}

void Database::initSchema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS inodes (
            id INTEGER PRIMARY KEY,
            filename TEXT UNIQUE,
            mode INTEGER,
            size INTEGER,
            mtime INTEGER
        );

        CREATE TABLE IF NOT EXISTS blocks (
            hash TEXT PRIMARY KEY,
            size INTEGER,
            refcount INTEGER
        );

        CREATE TABLE IF NOT EXISTS file_blocks (
            inode_id INTEGER,
            block_index INTEGER,
            block_hash TEXT,
            FOREIGN KEY(inode_id) REFERENCES inodes(id),
            FOREIGN KEY(block_hash) REFERENCES blocks(hash)
        );

        CREATE TABLE IF NOT EXISTS snapshots (
            id INTEGER PRIMARY KEY,
            timestamp INTEGER,
            parent_hash TEXT,
            commit_hash TEXT,
            message TEXT DEFAULT ''
        );
    )";
    executeQuery(schema);

    // Safe migration: add `message` column to existing databases that predate this schema.
    // ALTER TABLE ADD COLUMN is a no-op-safe operation in SQLite if the column already exists
    // would error, so we guard it with a pragma column check.
    auto colStmt = prepareStatement("PRAGMA table_info(snapshots);");
    bool hasMessageCol = false;
    while (sqlite3_step(colStmt.get()) == SQLITE_ROW) {
        const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(colStmt.get(), 1));
        if (colName && std::string(colName) == "message") {
            hasMessageCol = true;
            break;
        }
    }
    if (!hasMessageCol) {
        executeQuery("ALTER TABLE snapshots ADD COLUMN message TEXT DEFAULT '';");
    }
}

void Database::beginTransaction() {
    executeQuery("BEGIN TRANSACTION;");
}

void Database::commitTransaction() {
    executeQuery("COMMIT;");
}

void Database::rollbackTransaction() {
    executeQuery("ROLLBACK;");
}
// ---------------------------------------------------------------------------
// Metadata helpers
// ---------------------------------------------------------------------------

int Database::getInodeId(const std::string& filename)
{
    auto stmt = prepareStatement(
        "SELECT id FROM inodes WHERE filename = ?");

    sqlite3_bind_text(
        stmt.get(),
        1,
        filename.c_str(),
        -1,
        SQLITE_STATIC);

    int rc = sqlite3_step(stmt.get());

    if (rc == SQLITE_ROW)
    {
        return sqlite3_column_int(stmt.get(), 0);
    }

    if (rc == SQLITE_DONE)
    {
        throw std::runtime_error(
            "File does not exist: " + filename);
    }

    throw std::runtime_error(
        "Failed to lookup inode: " +
        std::string(sqlite3_errmsg(db.get())));
}
FileLayout Database::getCurrentFileLayout(int inodeId)
{
    FileLayout layout;

    //
    // Load logical size from inode metadata.
    //
    {
        auto stmt = prepareStatement(
            "SELECT size "
            "FROM inodes "
            "WHERE id = ?");

        sqlite3_bind_int(stmt.get(), 1, inodeId);

        if (sqlite3_step(stmt.get()) != SQLITE_ROW)
        {
            throw std::runtime_error(
                "Failed to lookup inode size.");
        }

        layout.logicalSize =
            static_cast<size_t>(
                sqlite3_column_int64(stmt.get(), 0));
    }
    {
        auto stmt = prepareStatement(
            "SELECT block_hash "
            "FROM file_blocks "
            "WHERE inode_id = ? "
            "ORDER BY block_index");

        sqlite3_bind_int(stmt.get(), 1, inodeId);

        while (true)
        {
            int rc = sqlite3_step(stmt.get());

            if (rc == SQLITE_DONE)
                break;

            if (rc != SQLITE_ROW)
            {
                throw std::runtime_error(
                    "Failed to lookup block mappings: " +
                    std::string(sqlite3_errmsg(db.get())));
            }

            layout.blockHashes.emplace_back(
                reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt.get(), 0)));
        }
    }

    return layout;
}
void Database::updateInodeMetadata(
    int inodeId,
    size_t newSize)
{
    auto stmt = prepareStatement(
        "UPDATE inodes "
        "SET size = ?, "
        "mtime = strftime('%s','now') "
        "WHERE id = ?");

    sqlite3_bind_int(
        stmt.get(),
        1,
        static_cast<int>(newSize));

    sqlite3_bind_int(
        stmt.get(),
        2,
        inodeId);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            "Failed to update inode metadata: " +
            std::string(sqlite3_errmsg(db.get())));
    }
}
void Database::replaceFileMappings(
    int inodeId,
    const FileLayout& layout)
{
    // Remove existing mappings.
    {
        auto stmt = prepareStatement(
            "DELETE FROM file_blocks "
            "WHERE inode_id = ?");

        sqlite3_bind_int(stmt.get(), 1, inodeId);

        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
        {
            throw std::runtime_error(
                "Failed to delete existing block mappings: " +
                std::string(sqlite3_errmsg(db.get())));
        }
    }

    // Insert replacement mappings.
    for (size_t i = 0; i < layout.blockHashes.size(); ++i)
    {
        auto stmt = prepareStatement(
            "INSERT INTO file_blocks "
            "(inode_id, block_index, block_hash) "
            "VALUES (?, ?, ?)");

        sqlite3_bind_int(stmt.get(), 1, inodeId);
        sqlite3_bind_int(stmt.get(), 2, static_cast<int>(i));

        sqlite3_bind_text(
            stmt.get(),
            3,
            layout.blockHashes[i].c_str(),
            -1,
            SQLITE_STATIC);

        if (sqlite3_step(stmt.get()) != SQLITE_DONE)
        {
            throw std::runtime_error(
                "Failed to insert block mapping: " +
                std::string(sqlite3_errmsg(db.get())));
        }
    }
}

std::vector<uint8_t> Database::loadFileContents(int inodeId, BlockStore& cas)
{
    FileLayout layout = getCurrentFileLayout(inodeId);
    std::vector<uint8_t> result;

    for (const auto& hash : layout.blockHashes)
    {
        auto block = cas.readBlock(hash);
        result.insert( result.end(), block.begin(), block.end());
    }

    result.resize(layout.logicalSize);

    return result;
}

FileLayout Database::storeFileContents(const std::vector<uint8_t>& data, BlockStore& cas)
{
    FileLayout layout;
    layout.logicalSize = data.size();

    auto blocks = BlockChunker::split(data);

    for (const auto& block : blocks)
    {
        layout.blockHashes.push_back( cas.writeBlock(block.bytes));
    }

    return layout;
}

void Database::commitFileContents( int inodeId, const std::vector<uint8_t>& data, BlockStore& cas) {
    
        FileLayout newLayout = storeFileContents(data, cas);
        const auto oldLayout = getCurrentFileLayout(inodeId);

        auto blocks = BlockChunker::split(data);

        for (size_t i = 0; i < newLayout.blockHashes.size(); ++i)
        {
            insertBlockMetadata(
                newLayout.blockHashes[i],
                blocks[i].bytes.size());

            incrementRefcount(
                newLayout.blockHashes[i]);
        }

        // Update inode → block mapping.
        replaceFileMappings(
            inodeId,
            newLayout);

        // Release ownership.

        std::vector<std::string> blocksToDelete;

        for (const auto& hash : oldLayout.blockHashes)
        {
            if (decrementRefcount(hash))
            {
                deleteBlockMetadata(hash);
                blocksToDelete.push_back(hash);
            }
        }

        updateInodeMetadata(
            inodeId,
            data.size());

        commitTransaction();

        #ifndef NDEBUG
        verifyReferenceCounts();
        #endif

        for (const auto& hash : blocksToDelete)
        {
            cas.deleteBlock(hash);
        }
}

void Database::writeFile(const std::string& filename, const char* buffer, size_t size, off_t offset, BlockStore& cas)
{
    beginTransaction();

    try
    {
        int inodeId = getInodeId(filename);

        std::vector<uint8_t> data = loadFileContents(inodeId, cas);

        size_t requiredSize = static_cast<size_t>(offset) + size;

        if (data.size() < requiredSize)
        {
            data.resize(requiredSize, 0);
        }

        std::copy(buffer, buffer + size, data.begin() + offset);

        commitFileContents(inodeId, data, cas);
    }
    catch (...)
    {
        rollbackTransaction();
        throw;
    }
}

std::vector<uint8_t> Database::readFile(
    const std::string& filename,
    BlockStore& cas)
{
    int inodeId = getInodeId(filename);

    return loadFileContents(
        inodeId,
        cas);
}


void Database::unlinkFile(
    const std::string& filename,
    BlockStore& cas)
{
    beginTransaction();

    std::vector<std::string> blocksToDelete;

    try
    {
        int inodeId = getInodeId(filename);

        //
        // Remember which blocks this file owns.
        //
        auto oldLayout =
            getCurrentFileLayout(inodeId);

        //
        // Remove mappings.
        //
        {
            auto stmt = prepareStatement(
                "DELETE FROM file_blocks "
                "WHERE inode_id = ?");

            sqlite3_bind_int(
                stmt.get(),
                1,
                inodeId);

            if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            {
                throw std::runtime_error(
                    "Failed to delete file mappings.");
            }
        }

        //
        // Remove inode.
        //
        {
            auto stmt = prepareStatement(
                "DELETE FROM inodes "
                "WHERE id = ?");

            sqlite3_bind_int(
                stmt.get(),
                1,
                inodeId);

            if (sqlite3_step(stmt.get()) != SQLITE_DONE)
            {
                throw std::runtime_error(
                    "Failed to delete inode.");
            }
        }

        //
        // Release ownership.
        //
        for (const auto& hash : oldLayout.blockHashes)
        {
            if (decrementRefcount(hash))
            {
                deleteBlockMetadata(hash);

                blocksToDelete.push_back(hash);
            }
        }

        commitTransaction();

#ifndef NDEBUG
        verifyReferenceCounts();
#endif

        //
        // Physical cleanup happens AFTER commit.
        //
        for (const auto& hash : blocksToDelete)
        {
            cas.deleteBlock(hash);
        }
    }
    catch (...)
    {
        rollbackTransaction();
        throw;
    }
}

void Database::insertBlockMetadata(
    const std::string& hash,
    size_t blockSize)
{
    auto stmt = prepareStatement(
        "INSERT OR IGNORE INTO blocks "
        "(hash, size, refcount) "
        "VALUES (?, ?, 0)");

    sqlite3_bind_text(
        stmt.get(),
        1,
        hash.c_str(),
        -1,
        SQLITE_STATIC);

    sqlite3_bind_int(
        stmt.get(),
        2,
        static_cast<int>(blockSize));

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            "Failed to insert block metadata: " +
            std::string(sqlite3_errmsg(db.get())));
    }
}

void Database::incrementRefcount(
    const std::string& hash)
{
    auto stmt = prepareStatement(
        "UPDATE blocks "
        "SET refcount = refcount + 1 "
        "WHERE hash = ?");

    sqlite3_bind_text(
        stmt.get(),
        1,
        hash.c_str(),
        -1,
        SQLITE_STATIC);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            "Failed to increment refcount: " +
            std::string(sqlite3_errmsg(db.get())));
    }

    if (sqlite3_changes(db.get()) != 1)
    {
        throw std::runtime_error(
            "incrementRefcount(): block metadata missing.");
    }
}


bool Database::decrementRefcount(const std::string& hash)
{
    auto stmt = prepareStatement(
        "UPDATE blocks "
        "SET refcount = refcount - 1 "
        "WHERE hash = ?");

    sqlite3_bind_text(
        stmt.get(),
        1,
        hash.c_str(),
        -1,
        SQLITE_STATIC);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            "Failed to decrement refcount: " +
            std::string(sqlite3_errmsg(db.get())));
    }
    if (sqlite3_changes(db.get()) != 1)
    {
        throw std::runtime_error(
            "decrementRefcount(): block metadata missing.");
    }

    auto query = prepareStatement(
        "SELECT refcount "
        "FROM blocks "
        "WHERE hash = ?");

    sqlite3_bind_text(
        query.get(),
        1,
        hash.c_str(),
        -1,
        SQLITE_STATIC);

    if (sqlite3_step(query.get()) != SQLITE_ROW)
    {
        throw std::runtime_error(
            "Failed to query refcount.");
    }

    return sqlite3_column_int(query.get(), 0) == 0;
}


void Database::deleteBlockMetadata(
    const std::string& hash)
{
    auto stmt = prepareStatement(
        "DELETE FROM blocks "
        "WHERE hash = ?");

    sqlite3_bind_text(
        stmt.get(),
        1,
        hash.c_str(),
        -1,
        SQLITE_STATIC);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE)
    {
        throw std::runtime_error(
            "Failed to delete block metadata: " +
            std::string(sqlite3_errmsg(db.get())));
    }
}


void Database::verifyReferenceCounts()
{
    auto stmt = prepareStatement(
        "SELECT hash, refcount "
        "FROM blocks");

    while (true)
    {
        int rc = sqlite3_step(stmt.get());

        if (rc == SQLITE_DONE)
            break;

        if (rc != SQLITE_ROW)
        {
            throw std::runtime_error(
                "verifyReferenceCounts(): failed to iterate blocks.");
        }

        std::string hash =
            reinterpret_cast<const char*>(
                sqlite3_column_text(stmt.get(), 0));

        int storedRefcount =
            sqlite3_column_int(stmt.get(), 1);

        auto countStmt = prepareStatement(
            "SELECT COUNT(*) "
            "FROM file_blocks "
            "WHERE block_hash = ?");

        sqlite3_bind_text(
            countStmt.get(),
            1,
            hash.c_str(),
            -1,
            SQLITE_STATIC);

        if (sqlite3_step(countStmt.get()) != SQLITE_ROW)
        {
            throw std::runtime_error(
                "verifyReferenceCounts(): failed to count references.");
        }

        int actualRefcount =
            sqlite3_column_int(
                countStmt.get(),
                0);

        if (storedRefcount != actualRefcount)
        {
            throw std::runtime_error(
                "Reference count mismatch for block " +
                hash +
                " (stored=" +
                std::to_string(storedRefcount) +
                ", actual=" +
                std::to_string(actualRefcount) +
                ")");
        }
    }
}


void Database::truncateFile(
    const std::string& filename,
    size_t newSize,
    BlockStore& cas)
{
    beginTransaction();

    try
    {
        int inodeId =
            getInodeId(filename);

        std::vector<uint8_t> data =
            loadFileContents(
                inodeId,
                cas);

        //
        // POSIX semantics:
        //
        // Shrinking removes bytes.
        // Growing appends zero bytes.
        //
        data.resize(newSize, 0);

        commitFileContents(
            inodeId,
            data,
            cas);
    }
    catch (...)
    {
        rollbackTransaction();
        throw;
    }
}








// ---------------------------------------------------------------------------
// .janusignore helpers
// ---------------------------------------------------------------------------

std::vector<std::string> Database::getIgnoreList(BlockStore& cas) {
    std::vector<std::string> rules;

    // 1. Find the .janusignore inode
    auto inodeStmt = prepareStatement(
        "SELECT i.id FROM inodes i WHERE i.filename = '.janusignore'"
    );
    if (sqlite3_step(inodeStmt.get()) != SQLITE_ROW) {
        return rules;  // no .janusignore present — nothing to ignore
    }
    sqlite3_int64 inode_id = sqlite3_column_int64(inodeStmt.get(), 0);

    // 2. Get the block_hash for that inode
    auto blockStmt = prepareStatement(
        "SELECT block_hash FROM file_blocks WHERE inode_id = ? LIMIT 1"
    );
    sqlite3_bind_int64(blockStmt.get(), 1, inode_id);
    if (sqlite3_step(blockStmt.get()) != SQLITE_ROW) {
        return rules;  // inode exists but has no data block
    }
    const char* raw_hash = reinterpret_cast<const char*>(sqlite3_column_text(blockStmt.get(), 0));
    if (!raw_hash) return rules;
    std::string block_hash(raw_hash);

    // 3. Read the block and split by newlines into rules
    std::vector<uint8_t> data = cas.readBlock(block_hash);
    std::string content(data.begin(), data.end());

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        // Trim trailing carriage return (Windows line endings)
        if (!line.empty() && static_cast<unsigned char>(line.back()) == 13) line.pop_back();
        if (!line.empty()) {
            rules.push_back(line);
        }
    }
    return rules;
}

bool Database::isIgnored(const std::string& filename,
                          const std::vector<std::string>& ignoreList) {
    for (const auto& rule : ignoreList) {
        // Exact match
        if (filename == rule) return true;

        // Glob-style suffix: *.ext  →  check filename ends with .ext
        if (rule.size() > 1 && rule[0] == '*') {
            const std::string suffix = rule.substr(1);  // e.g. "*.log" → ".log"
            if (filename.size() >= suffix.size() &&
                filename.compare(filename.size() - suffix.size(),
                                 suffix.size(), suffix) == 0) {
                return true;
            }
        }
    }
    return false;
}


std::string Database::commitSnapshot(BlockStore& cas, const std::string& message, const std::string& parent_hash) {
    beginTransaction();
    std::string commit_hash;

    // Fetch ignore rules before scanning inodes (outside the manifest loop)
    std::vector<std::string> ignoreList = getIgnoreList(cas);

    try {
        auto stmt = prepareStatement(
            "SELECT i.filename, i.size, i.mode, fb.block_hash "
            "FROM inodes i LEFT JOIN file_blocks fb ON i.id = fb.inode_id"
        );

        std::stringstream manifest;
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            const char* filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
            int size = sqlite3_column_int(stmt.get(), 1);
            int mode = sqlite3_column_int(stmt.get(), 2);
            const char* block_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));

            std::string fname = filename ? filename : "";

            // Skip files that match a .janusignore rule
            if (isIgnored(fname, ignoreList)) continue;

            std::string block_hash_str = block_hash ? block_hash : "EMPTY";

            manifest << "FILE|" << fname << "|"
                     << size << "|" << mode << "|" << block_hash_str << "\n";
        }

        std::string manifest_str = manifest.str();
        std::vector<uint8_t> data(manifest_str.begin(), manifest_str.end());
        commit_hash = cas.writeBlock(data);

        auto insertStmt = prepareStatement(
            "INSERT INTO snapshots (timestamp, parent_hash, commit_hash, message) VALUES (?, ?, ?, ?)"
        );
        sqlite3_bind_int64(insertStmt.get(), 1, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_bind_text(insertStmt.get(), 2, parent_hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt.get(), 3, commit_hash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt.get(), 4, message.c_str(),      -1, SQLITE_STATIC);

        if (sqlite3_step(insertStmt.get()) != SQLITE_DONE) {
            std::string err = sqlite3_errmsg(db.get());
            throw std::runtime_error("Failed to insert snapshot record: " + err);
        }

        commitTransaction();
    } catch (...) {
        rollbackTransaction();
        throw;
    }

    return commit_hash;
}

std::string Database::getLatestSnapshotHash() {
    auto stmt = prepareStatement("SELECT commit_hash FROM snapshots ORDER BY id DESC LIMIT 1");
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        return hash ? std::string(hash) : "";
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> Database::getSnapshotHistory() {
    std::vector<std::pair<std::string, std::string>> history;
    auto stmt = prepareStatement(
        "SELECT commit_hash, message, timestamp FROM snapshots ORDER BY id DESC"
    );
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
        const char* msg  = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
        history.emplace_back(
            hash ? hash : "",
            (msg && msg[0]) ? msg : "(no message)"
        );
    }
    return history;
}

void Database::printStats(bool asJson) {
    // ── Query 1: total tracked files ────────────────────────────────────
    auto filesStmt = prepareStatement("SELECT COUNT(*) FROM inodes;");
    sqlite3_step(filesStmt.get());
    int64_t totalFiles = sqlite3_column_int64(filesStmt.get(), 0);

    // ── Query 2: unique CAS blocks referenced by any file ───────────────
    auto blocksStmt = prepareStatement(
        "SELECT COUNT(DISTINCT block_hash) FROM file_blocks;"
    );
    sqlite3_step(blocksStmt.get());
    int64_t uniqueBlocks = sqlite3_column_int64(blocksStmt.get(), 0);

    // ── Query 3: total logical size (sum of all inode sizes) ─────────────
    auto sizeStmt = prepareStatement("SELECT COALESCE(SUM(size), 0) FROM inodes;");
    sqlite3_step(sizeStmt.get());
    int64_t totalBytes = sqlite3_column_int64(sizeStmt.get(), 0);

    // ── Query 4: total snapshots taken ───────────────────────────────────
    auto snapStmt = prepareStatement("SELECT COUNT(*) FROM snapshots;");
    sqlite3_step(snapStmt.get());
    int64_t totalSnapshots = sqlite3_column_int64(snapStmt.get(), 0);

    if (asJson) {
        // Strict JSON — no external library needed
        std::cout << "{"
                  << "\"files\": "     << totalFiles    << ", "
                  << "\"blocks\": "    << uniqueBlocks  << ", "
                  << "\"total_bytes\": " << totalBytes  << ", "
                  << "\"snapshots\": " << totalSnapshots
                  << "}\n";
    } else {
        // Human-readable ANSI output
        std::cout << "\n\033[1;36m  JANUS \u2014 Filesystem Statistics\033[0m\n";
        std::cout << "\033[90m  " << std::string(40, '-') << "\033[0m\n";

        std::cout << "  \033[1mTracked Files   :\033[0m \033[33m" << totalFiles    << "\033[0m\n";
        std::cout << "  \033[1mUnique CAS Blocks:\033[0m \033[33m" << uniqueBlocks  << "\033[0m\n";
        std::cout << "  \033[1mTotal Snapshots  :\033[0m \033[33m" << totalSnapshots << "\033[0m\n";

        // Human-readable size
        double displaySize = static_cast<double>(totalBytes);
        const char* sizeUnit = "bytes";
        if (totalBytes >= 1024LL * 1024 * 1024) {
            displaySize /= 1024.0 * 1024.0 * 1024.0;
            sizeUnit = "GiB";
        } else if (totalBytes >= 1024 * 1024) {
            displaySize /= 1024.0 * 1024.0;
            sizeUnit = "MiB";
        } else if (totalBytes >= 1024) {
            displaySize /= 1024.0;
            sizeUnit = "KiB";
        }

        // Print size with up to 2 decimal places when not in bytes
        if (std::string(sizeUnit) == "bytes") {
            std::cout << "  \033[1mLogical Size     :\033[0m \033[33m" << totalBytes << " bytes\033[0m\n";
        } else {
            std::cout << "  \033[1mLogical Size     :\033[0m \033[33m"
                      << std::fixed << std::setprecision(2) << displaySize
                      << " " << sizeUnit << "  (" << totalBytes << " bytes)\033[0m\n";
        }

        // Deduplication ratio: logical references vs unique blocks
        if (uniqueBlocks > 0 && totalFiles > 0) {
            double ratio = static_cast<double>(totalFiles) / static_cast<double>(uniqueBlocks);
            std::cout << "  \033[1mDedup Ratio      :\033[0m \033[32m"
                      << std::fixed << std::setprecision(2) << ratio
                      << "x\033[0m  \033[90m(" << totalFiles << " files \u2192 "
                      << uniqueBlocks << " unique blocks)\033[0m\n";
        }

        std::cout << "\033[90m  " << std::string(40, '-') << "\033[0m\n\n";
    }
}

void Database::checkoutSnapshot(BlockStore& cas, const std::string& commit_hash) {
    std::vector<uint8_t> data = cas.readBlock(commit_hash);
    std::string manifest(data.begin(), data.end());

    // --- Phase 1: collect ignored files BEFORE wiping the database ---
    std::vector<std::string> ignoreList = getIgnoreList(cas);

    struct SavedInode {
        std::string filename;
        int         size;
        int         mode;
        std::string block_hash;  // may be "EMPTY"
    };
    std::vector<SavedInode> preserved;

    {
        auto scanStmt = prepareStatement(
            "SELECT i.filename, i.size, i.mode, fb.block_hash "
            "FROM inodes i LEFT JOIN file_blocks fb ON i.id = fb.inode_id"
        );
        while (sqlite3_step(scanStmt.get()) == SQLITE_ROW) {
            const char* fn  = reinterpret_cast<const char*>(sqlite3_column_text(scanStmt.get(), 0));
            const char* bh  = reinterpret_cast<const char*>(sqlite3_column_text(scanStmt.get(), 3));
            std::string fname = fn ? fn : "";
            if (isIgnored(fname, ignoreList)) {
                preserved.push_back({
                    fname,
                    sqlite3_column_int(scanStmt.get(), 1),
                    sqlite3_column_int(scanStmt.get(), 2),
                    bh ? std::string(bh) : "EMPTY"
                });
            }
        }
    }

    // --- Phase 2: wipe + restore snapshot + re-insert preserved inodes ---
    beginTransaction();
    try {
        executeQuery("DELETE FROM file_blocks;");
        executeQuery("DELETE FROM inodes;");

        // Restore snapshot manifest entries
        std::istringstream iss(manifest);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            std::vector<std::string> tokens;
            std::string token;
            std::istringstream line_stream(line);
            while (std::getline(line_stream, token, '|')) {
                tokens.push_back(token);
            }

            if (tokens.size() != 5 || tokens[0] != "FILE") {
                throw std::runtime_error("Malformed manifest line: " + line);
            }

            std::string filename   = tokens[1];
            int         size       = std::stoi(tokens[2]);
            int         mode       = std::stoi(tokens[3]);
            std::string block_hash = tokens[4];

            auto insertInode = prepareStatement(
                "INSERT INTO inodes (filename, mode, size, mtime) VALUES (?, ?, ?, strftime('%s','now'))"
            );
            sqlite3_bind_text(insertInode.get(), 1, filename.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertInode.get(), 2, mode);
            sqlite3_bind_int(insertInode.get(), 3, size);

            if (sqlite3_step(insertInode.get()) != SQLITE_DONE) {
                const char* err = sqlite3_errmsg(db.get());
                throw std::runtime_error(std::string("Failed to insert inode: ") + (err ? err : ""));
            }

            sqlite3_int64 inode_id = sqlite3_last_insert_rowid(db.get());

            if (block_hash != "EMPTY") {
                auto insertBlock = prepareStatement(
                    "INSERT INTO file_blocks (inode_id, block_index, block_hash) VALUES (?, 0, ?)"
                );
                sqlite3_bind_int64(insertBlock.get(), 1, inode_id);
                sqlite3_bind_text(insertBlock.get(), 2, block_hash.c_str(), -1, SQLITE_STATIC);

                if (sqlite3_step(insertBlock.get()) != SQLITE_DONE) {
                    const char* err = sqlite3_errmsg(db.get());
                    throw std::runtime_error(std::string("Failed to insert file block mapping: ") + (err ? err : ""));
                }

                auto blockStmt = prepareStatement("INSERT OR IGNORE INTO blocks (hash, size, refcount) VALUES (?, ?, 1)");
                sqlite3_bind_text(blockStmt.get(), 1, block_hash.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(blockStmt.get(), 2, size);
                sqlite3_step(blockStmt.get());
            }
        }

        // --- Phase 3: re-insert preserved ignored files ---
        for (const auto& saved : preserved) {
            // Use INSERT OR IGNORE so a snapshot file with the same name takes priority
            auto insertInode = prepareStatement(
                "INSERT OR IGNORE INTO inodes (filename, mode, size, mtime) VALUES (?, ?, ?, strftime('%s','now'))"
            );
            sqlite3_bind_text(insertInode.get(), 1, saved.filename.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(insertInode.get(), 2, saved.mode);
            sqlite3_bind_int(insertInode.get(), 3, saved.size);

            if (sqlite3_step(insertInode.get()) != SQLITE_DONE) {
                const char* err = sqlite3_errmsg(db.get());
                throw std::runtime_error(std::string("Failed to restore ignored inode: ") + (err ? err : ""));
            }

            // Only insert block mapping if the row was actually inserted (not already present)
            if (sqlite3_changes(db.get()) > 0 && saved.block_hash != "EMPTY") {
                sqlite3_int64 inode_id = sqlite3_last_insert_rowid(db.get());

                auto insertBlock = prepareStatement(
                    "INSERT INTO file_blocks (inode_id, block_index, block_hash) VALUES (?, 0, ?)"
                );
                sqlite3_bind_int64(insertBlock.get(), 1, inode_id);
                sqlite3_bind_text(insertBlock.get(), 2, saved.block_hash.c_str(), -1, SQLITE_STATIC);

                if (sqlite3_step(insertBlock.get()) != SQLITE_DONE) {
                    const char* err = sqlite3_errmsg(db.get());
                    throw std::runtime_error(std::string("Failed to restore ignored file blocks: ") + (err ? err : ""));
                }

                auto blockStmt = prepareStatement("INSERT OR IGNORE INTO blocks (hash, size, refcount) VALUES (?, ?, 1)");
                sqlite3_bind_text(blockStmt.get(), 1, saved.block_hash.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_int(blockStmt.get(), 2, saved.size);
                sqlite3_step(blockStmt.get());
            }
        }
        commitTransaction();
        verifyReferenceCounts();
    } catch (...) {
        rollbackTransaction();
        throw;
    }
}

void Database::diffSnapshots(BlockStore& cas, const std::string& hash1, const std::string& hash2) {
    // Fetch raw manifest bytes for both snapshots
    std::vector<uint8_t> data1 = cas.readBlock(hash1);
    std::vector<uint8_t> data2 = cas.readBlock(hash2);

    std::string manifest1(data1.begin(), data1.end());
    std::string manifest2(data2.begin(), data2.end());

    // Helper: parse a manifest string into { filename -> block_hash }
    auto parseManifest = [](const std::string& manifest) -> std::unordered_map<std::string, std::string> {
        std::unordered_map<std::string, std::string> map;
        std::istringstream iss(manifest);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            std::vector<std::string> tokens;
            std::string token;
            std::istringstream ls(line);
            while (std::getline(ls, token, '|')) {
                tokens.push_back(token);
            }

            // Expected format: FILE|<filename>|<size>|<mode>|<block_hash>
            if (tokens.size() == 5 && tokens[0] == "FILE") {
                map[tokens[1]] = tokens[4];  // filename -> block_hash
            }
        }
        return map;
    };

    auto map1 = parseManifest(manifest1);  // older snapshot
    auto map2 = parseManifest(manifest2);  // newer snapshot

    // Detect additions and modifications
    for (const auto& [filename, block_hash] : map2) {
        auto it = map1.find(filename);
        if (it == map1.end()) {
            std::cout << "\033[32m[+] Added:    " << filename << "\033[0m\n";
        } else if (it->second != block_hash) {
            std::cout << "\033[33m[~] Modified: " << filename << "\033[0m\n";
        }
    }

    // Detect deletions
    for (const auto& [filename, block_hash] : map1) {
        if (map2.find(filename) == map2.end()) {
            std::cout << "\033[31m[-] Removed:  " << filename << "\033[0m\n";
        }
    }
}
