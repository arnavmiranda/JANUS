# JANUS: A Version-Controlled Content-Addressable Filesystem in Userspace

## Abstract

**JANUS** is a custom user-space filesystem implemented in **C++17** that converges virtual filesystem (VFS) architecture, relational database theory, and cryptographic deduplication into a cohesive storage engine. By intercepting POSIX system calls via **FUSE (Filesystem in Userspace)**, JANUS abstracts traditional block-device storage, decoupling file metadata from payload data.

Unlike traditional filesystems, JANUS features intrinsic **4KB block-level data deduplication**, **ACID-compliant metadata persistence**, **real-time garbage collection**, and **deterministic temporal version control** directly at the filesystem interface level.

---

# 🚀 Key Capabilities

### Block-Level Deduplication

Files are sliced into **4096-byte chunks**. Each chunk is hashed using **SHA-256** and stored uniquely.

> A 1GB file with a 1-byte modification consumes only an additional **4KB** of physical storage.

### Time-Travel / Version Control

- Native filesystem snapshots
- Instant checkout of historical states
- Git-like version control semantics integrated into the filesystem

### Active Garbage Collection

JANUS maintains strict SQLite reference counts on all CAS blocks.

When files are:

- modified
- deleted
- truncated

orphaned blocks are automatically reclaimed.

### Dynamic State Preservation (`.janusignore`)

Ignored files are excluded from snapshots while remaining preserved across checkouts.

Typical use cases include:

- secrets
- log files
- runtime configuration

### Interactive TUI

Built-in terminal interfaces provide:

- Snapshot history
- Interactive checkout
- Filesystem navigation

---

# 🧠 System Architecture

JANUS consists of three primary subsystems.

## 1. FUSE Intercept Layer (VFS)

Using **libfuse3**, JANUS intercepts kernel POSIX operations including:

- `getattr`
- `readdir`
- `read`
- `write`
- `create`
- `unlink`
- `truncate`

These operations are redirected into custom C++ handlers while exposing a fully POSIX-compliant filesystem interface.

---

## 2. Relational Metadata Engine (SQLite)

Rather than implementing traditional inode trees, JANUS delegates metadata management to an embedded SQLite database (`janus_meta.db`).

### ACID Compliance

SQLite operates in:

- WAL mode
- Foreign keys enabled

ensuring:

- Atomicity
- Consistency
- Isolation
- Durability

### Relational Mapping

Metadata stored includes:

- POSIX permissions
- Logical file size
- Modification timestamps
- File → CAS block mappings (`file_blocks`)

---

## 3. Content-Addressable Storage (CAS)

File payloads are never stored linearly.

During writes:

1. `BlockChunker` splits data into **4096-byte blocks**
2. Every block is hashed using **OpenSSL SHA-256**
3. The hash becomes the filename inside:

```
.janus/blocks/
```

If the block already exists:

- physical write is skipped
- deduplication occurs automatically

During reads:

`FileAssembler` reconstructs files dynamically from CAS blocks.

---

# ⏱️ Version Control Mechanics

Version control is implemented directly inside the filesystem.

## Commit

Taking a snapshot:

- scans active files
- ignores `.janusignore`
- serializes filesystem state
- hashes the resulting manifest
- stores it as another CAS object
- records metadata in the SQLite `snapshots` table

---

## Checkout

Restoring a snapshot performs:

1. Cache ignored files
2. Destroy current metadata mappings
3. Parse historical manifest
4. Restore filesystem state
5. Reinject ignored files

This preserves secrets, logs, and local runtime files.

---

## Diff

Two snapshot manifests are compared to identify:

- Added files
- Removed files
- Modified files

---

# 🛠️ Prerequisites

## Dependencies

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    libfuse3-dev \
    libsqlite3-dev \
    libssl-dev \
    pkg-config
```

---

## Build

```bash
mkdir build
cd build

cmake ..
make
```

---

# 🕹️ Operational Lifecycle

JANUS requires two terminal sessions.

---

## Terminal A — Mount Filesystem

```bash
mkdir -p /tmp/mt
./janus mount /tmp/mt
```

The metadata engine and CAS storage will initialize here.

---

## Terminal B — Filesystem Usage

Create ignore rules:

```bash
echo "*.log" > /tmp/mt/.janusignore
echo "secret.key" >> /tmp/mt/.janusignore
```

Create a file:

```bash
echo "Primary Application Logic" > /tmp/mt/main.cpp
```

Commit a snapshot:

```bash
./janus commit -m "Initial system state"
```

---

# 📜 Snapshot History

Launch the interactive history viewer:

```bash
./janus log
```

Example:

```text
JANUS — Snapshot History
----------------------------------------------------
[0] a3f8b9e1c2d3... | Initial system state
----------------------------------------------------

Enter snapshot number to checkout (or 'q' to quit): 0
```

---

## Manual Operations

Compare snapshots:

```bash
./janus diff <HASH_1> <HASH_2>
```

Restore a snapshot:

```bash
./janus checkout <HASH_1>
```

---

# 📊 Filesystem Statistics

View deduplication metrics:

```bash
./janus stats
```

Example:

```text
JANUS — Filesystem Statistics
----------------------------------------
Tracked Files      : 14
Unique CAS Blocks  : 8
Total Snapshots    : 3
Logical Size       : 42.15 KiB (43162 bytes)
Dedup Ratio        : 1.75x (14 files → 8 unique blocks)
```

For machine-readable output:

```bash
./janus stats --json
```

---

# ⌨️ Bash Autocompletion

Enable for the current shell:

```bash
source ./janus-completion.bash
```

Install permanently:

```bash
echo "source $(pwd)/janus-completion.bash" >> ~/.bashrc
source ~/.bashrc
```

---

# 📂 Repository Structure

```text
janus/
├── src/
│   ├── main.cpp            # Command router, interactive TUIs, and FUSE init
│   ├── JanusFS.cpp         # POSIX-to-C++ FUSE intercept handlers
│   ├── Database.cpp        # SQLite metadata, GC refcounting, diffing, rollbacks
│   ├── BlockStore.cpp      # OpenSSL SHA-256 CAS engine (Physical I/O)
│   ├── BlockChunker.cpp    # Splits data into 4KB deduplication blocks
│   ├── FileAssembler.cpp   # Reconstructs files dynamically from CAS blocks
│   ├── Repository.cpp      # Orchestrator binding FS calls to DB/Storage
│   └── StorageEngine.cpp   # Abstraction for chunking and garbage collection
├── include/
├── CMakeLists.txt
├── janus-completion.bash
└── README.md
```

---

# 🧹 System Teardown

Unmount the filesystem:

```bash
sudo umount -l /tmp/mt
```

Remove metadata and CAS storage:

```bash
rm -rf build/janus_meta.db* build/.janus/
```