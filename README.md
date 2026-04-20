# JANUS: Version-Controlled Content-Addressable Filesystem

![C++](https://img.shields.io/badge/C++-17-blue.svg)
![FUSE](https://img.shields.io/badge/FUSE-3.14-orange.svg)
![SQLite](https://img.shields.io/badge/SQLite-WAL_Mode-lightblue.svg)
![OpenSSL](https://img.shields.io/badge/OpenSSL-SHA256-brightgreen.svg)
![CMake](https://img.shields.io/badge/Build-CMake-red.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-yellowgreen.svg)

JANUS is a custom user-space filesystem built in C++17 using FUSE (Filesystem in Userspace). It acts as a hybrid between a standard mounted drive and a Git-style version control system: every file operation is intercepted, content is stored in a deduplicated block pool, and the entire filesystem state can be snapshotted and rewound to any point in history with a single command.

---

## Core Architecture & Features

### 1. FUSE Kernel Bridge (`JanusFS`)
JANUS intercepts native Linux POSIX syscalls — `getattr`, `readdir`, `read`, `write`, `create`, and `unlink` — and routes them through custom C++ handlers in `JanusFS`. Static wrapper functions (`wrap_*`) are registered with `fuse_main` and relay calls into the `JanusFS` instance via the FUSE private-data pointer.

### 2. Relational Metadata Engine (`Database` / SQLite)
File metadata — inodes, permissions, sizes, and block mappings — is managed in a SQLite database (`janus_meta.db`). Four tables form the schema:

| Table | Purpose |
|---|---|
| `inodes` | One row per file: `filename`, `mode`, `size`, `mtime` |
| `file_blocks` | Maps `inode_id → block_hash` (one row per block, preserving order via `block_index`) |
| `blocks` | CAS catalogue: `hash`, `size`, `refcount` |
| `snapshots` | Immutable snapshot log: `timestamp`, `parent_hash`, `commit_hash` |

The database runs in **WAL (Write-Ahead Logging)** mode with `PRAGMA foreign_keys=ON`, providing corruption resistance during power losses or kernel panics and serializing multi-reader/single-writer concurrent access safely.

### 3. Content-Addressable Storage — CAS (`BlockStore` / OpenSSL)
File payloads are never stored linearly on disk. Every write:
1. Computes a **SHA-256 digest** of the raw bytes using the OpenSSL `EVP_DigestInit/Update/Final` API (via a RAII `EvpMdCtxPtr` wrapper).
2. Checks if `.janus/blocks/<hash>` already exists — if so, it returns immediately (**instant deduplication**).
3. Atomically writes via a randomised `.tmp` file and `std::filesystem::rename` (crash-safe, never leaves partial blocks).

100 copies of the same file consume the physical disk space of exactly one file.

### 4. Merkle-Tree Snapshotting (`commit`)
`commitSnapshot()` generates a lightweight text manifest — one `FILE|<name>|<size>|<mode>|<block_hash>` line per tracked inode — and stores the manifest itself as a CAS block. The SHA-256 hash of that manifest block is the **commit hash**, forming the root of a Merkle tree over the entire filesystem state.

Files listed in `.janusignore` are silently excluded from the manifest at commit time.

### 5. Timeline Reconstruction (`checkout`)
`checkoutSnapshot()` deserialises a historical manifest and performs an atomic 3-phase database rewrite:

1. **Preserve** — scan current inodes and save any `.janusignore`-matching files to an in-memory `vector<SavedInode>` before touching the database.
2. **Wipe & Restore** — `DELETE FROM file_blocks / inodes`, then re-insert every row from the target manifest.
3. **Re-inject preserved files** — re-insert saved ignored inodes using `INSERT OR IGNORE` so the snapshot takes priority on name collision.

The entire operation runs inside a single SQLite transaction; a `catch (...)` block triggers `ROLLBACK` on any failure, guaranteeing crash consistency.

### 6. Timeline Comparison (`diff`)
`diffSnapshots()` fetches two manifest blobs from the CAS, parses each into an `unordered_map<filename, block_hash>` in memory, and emits a colour-coded diff to stdout — no mounting required:

| Symbol | Colour | Meaning |
|---|---|---|
| `[+]` | Green | File added in the newer snapshot |
| `[~]` | Yellow | File modified (hash changed) |
| `[-]` | Red | File removed from the newer snapshot |

### 7. The Ignore Engine (`.janusignore`)
`getIgnoreList()` reads the `.janusignore` inode directly from the live database and its content from the CAS block pool, splitting the contents into a rule list. `isIgnored()` enforces two matching strategies:

- **Exact match** — `secret.key` matches only `secret.key`
- **Glob suffix** — `*.log` matches any filename ending in `.log`

---

## 🛠 Prerequisites & Build

Requires a **Linux** environment (Ubuntu 22.04+ recommended).

### Install Dependencies

```bash
sudo apt update
sudo apt install -y build-essential cmake libfuse3-dev libsqlite3-dev libssl-dev pkg-config
```

### Build

```bash
git clone <repo-url> janus
cd janus
cmake -B build
cmake --build build
```

The compiled binary is at `build/janus`.

---

##  Usage

All commands are run from the **same directory** where you want the filesystem to live. Janus creates two artefacts in that directory:
- `janus_meta.db` — SQLite metadata store
- `.janus/blocks/` — CAS block pool

### Mount the filesystem

```bash
./build/janus mount <mountpoint>
```

Mounts the FUSE filesystem at `<mountpoint>`. Runs in the foreground (`-f`). Open a second terminal to interact with files.

```bash
mkdir /tmp/mnt
./build/janus mount /tmp/mnt

# In another terminal:
echo "hello world" > /tmp/mnt/hello.txt
cat /tmp/mnt/hello.txt
ls /tmp/mnt
```

To unmount: `fusermount3 -u /tmp/mnt`

### Commit a snapshot

```bash
./build/janus commit
# Committed snapshot: a3f8c2d1e4b7...
```

Generates a manifest of all tracked inodes (excluding `.janusignore` entries) and writes it as a CAS block. Prints the commit hash.

### View snapshot history

```bash
./build/janus log
# Snapshot ID: 3 | Time: 1713614400 | Hash: a3f8c2d1e4b7...
# Snapshot ID: 2 | Time: 1713610800 | Hash: 9e1d5f6a2c0b...
# Snapshot ID: 1 | Time: 1713607200 | Hash: 7b2a4e8c1f3d...
```

Lists all snapshots in reverse-chronological order.

### Restore a snapshot

```bash
./build/janus checkout <hash>
# Successfully checked out snapshot: a3f8c2d1e4b7...
```

Atomically reconstructs the filesystem's past state. Files matching `.janusignore` rules are preserved through the rewrite.

### Diff two snapshots

```bash
./build/janus diff <hash1> <hash2>
```

`hash1` is the **older** baseline; `hash2` is the **newer** state. Example output:

```
[+] Added:    newfeature.cpp
[~] Modified: config.cfg
[-] Removed:  deprecated.log
```

---

## Project Structure

```
janus/
├── CMakeLists.txt          # CMake build — C++17, FUSE3, SQLite3, libcrypto
├── src/
│   ├── main.cpp            # CLI entrypoint — command dispatcher
│   ├── JanusFS.h / .cpp    # FUSE handler layer (getattr, readdir, read, write, create, unlink)
│   ├── Database.h / .cpp   # SQLite metadata engine + snapshot/checkout/diff/.janusignore logic
│   └── BlockStore.h / .cpp # CAS engine — SHA-256 hashing, atomic block I/O, deduplication
└── build/                  # CMake output directory (git-ignored)
```

---

## Crash Consistency Guarantees

| Layer | Mechanism |
|---|---|
| Metadata | SQLite WAL mode; all multi-step mutations wrapped in explicit `BEGIN / COMMIT / ROLLBACK` |
| Block writes | Randomised `.tmp` file + `std::filesystem::rename` (atomic at the VFS level) |
| Checkout | Single transaction covering all three phases; rolls back entirely on any error |
| RAII | `unique_ptr` custom deleters for `sqlite3*`, `sqlite3_stmt*`, and `EVP_MD_CTX*` — no resource leaks on exception paths |

---

## Design Decisions & Trade-offs

- **Single-block files** — each inode maps to exactly one `file_blocks` row (`block_index = 0`). Multi-block chunking is a natural future extension.
- **Flat namespace** — the filesystem presents a single directory. Subdirectory support would require a parent-inode column and recursive `readdir` logic.
- **Manifest as CAS blob** — storing snapshots as content-addressed objects means the snapshot log itself is deduplicated; identical filesystem states produce the same commit hash.
- **Ignore rules are live** — `.janusignore` is read from the live database on every `commit` and `checkout`, so rules take effect immediately without restarting the daemon.

