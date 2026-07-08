# JANUS

A userspace storage engine exploring content-addressable storage, snapshot-based versioning, and SQLite-backed metadata through FUSE.

---

## Overview

JANUS is an experimental userspace filesystem written in **modern C++17** using **FUSE3**.

Rather than implementing a traditional block-device filesystem, JANUS explores an alternative storage architecture where:

- File metadata is persisted in **SQLite**
- File contents are stored in a **SHA-256 content-addressable block store**
- Filesystem snapshots are implemented through **immutable manifest serialization**

The project serves as an exploration of filesystem architecture, storage engines, content-addressable storage, and snapshot-based version control rather than a fully POSIX-compliant filesystem.

---

## Goals

The primary objective of JANUS is to investigate how concepts from operating systems, databases, and version control can be combined into a modular storage engine.

The project explores:

- FUSE-based userspace filesystems
- SQLite as a metadata engine
- SHA-256 content-addressable storage
- Block-level deduplication
- Snapshot serialization
- Immutable storage
- Layered storage architecture

---

## Architecture

```text
                POSIX Applications
                       │
                  FUSE Interface
                       │
                  JanusFS Layer
                       │
                  Repository
           ┌───────────┴────────────┐
           │                        │
     SQLite Database         Storage Engine
           │                        │
      Metadata Tables       Chunker / Assembler
                                    │
                              Block Store (CAS)
```

---

## Components

### JanusFS

Implements the FUSE interface and translates supported filesystem operations into repository calls.

Currently implemented operations include:

- `getattr`
- `readdir`
- `create`
- `read`
- `write`
- `truncate`
- `unlink`

These operations provide a minimal working filesystem interface suitable for experimentation.

---

### Repository

Acts as the coordination layer between the virtual filesystem, metadata database, and storage engine.

Responsibilities include:

- Transactional writes
- File reconstruction
- Snapshot management
- Metadata updates

---

### SQLite Metadata Engine

File metadata is stored in SQLite operating in **WAL mode**.

Metadata includes:

- Filenames
- Permissions
- Logical file sizes
- Modification timestamps
- Block mappings

SQLite transactions provide atomic metadata updates during filesystem operations.

---

### Storage Engine

The storage engine converts files into layouts consisting of fixed-size blocks.

Each layout records:

- Logical file size
- Ordered block hashes
- Block sizes

The current implementation reconstructs and regenerates the complete layout during each write operation.

This favors implementation simplicity over write performance.

---

### Content-Addressable Storage

Each block is hashed using **SHA-256**.

Blocks are stored as immutable objects named by their hash.

Features include:

- Automatic block deduplication
- Immutable block storage
- Atomic temporary-file + rename semantics for safe writes

---

### Snapshot System

Snapshots capture the current logical filesystem state.

Each snapshot contains:

- Filenames
- Permissions
- Logical file sizes
- Ordered block hashes

Snapshots are serialized into immutable manifest objects stored inside the content-addressable storage layer.

Supported commands include:

- `commit`
- `checkout`
- `diff`
- `log`

The snapshot implementation is intentionally lightweight and should be viewed as filesystem checkpointing rather than a full Git-compatible version control system.

---

## Current Limitations

JANUS is intentionally a research project and does not currently implement the full POSIX filesystem interface.

Current limitations include:

- Flat namespace (no nested directories)
- No `mkdir` / `rmdir` support
- No `rename` support
- No `chmod` or `utimens` handlers
- No symbolic links or hard links
- Coarse-grained global locking
- Fixed-size chunking
- Full-file reconstruction during writes
- Snapshot garbage collection is not yet implemented

These limitations are known and represent future development goals rather than design oversights.

---

## Why SQLite?

Rather than implementing a custom metadata storage engine, JANUS delegates metadata persistence to SQLite.

Benefits include:

- Transactional updates
- Crash recovery through WAL
- Relational metadata management
- Simplified implementation

This allows the project to focus on storage architecture rather than database internals.

---

## Why Content-Addressable Storage?

Using immutable hash-addressed blocks naturally enables:

- Transparent deduplication
- Immutable storage
- Deterministic snapshot serialization
- Content verification

The current implementation uses fixed-size **4 KB** blocks.

Future work includes evaluating content-defined chunking techniques such as **FastCDC**.

---

## Building

Install dependencies:

```bash
sudo apt install \
    build-essential \
    cmake \
    libfuse3-dev \
    libsqlite3-dev \
    libssl-dev
```

Build the project:

```bash
mkdir build
cd build

cmake ..
make
```

---

## Running

Mount the filesystem:

```bash
./janus mount /tmp/mt
```

Example usage:

```bash
echo "hello" > /tmp/mt/file.txt

./janus commit -m "Initial snapshot"

./janus log

./janus diff HASH1 HASH2

./janus checkout HASH1
```

---

## Future Work

Planned improvements include:

- Hierarchical directory support
- Rename and metadata operations
- Finer-grained locking
- Incremental block updates
- Content-defined chunking
- Safe snapshot-aware garbage collection
- Background block reclamation
- Improved testing and benchmarking
- Compression
- Larger-scale storage benchmarking

---

## Technologies

- C++17
- FUSE3
- SQLite3
- OpenSSL
- CMake

---

## Project Status

JANUS is an active experimental storage engine intended for learning and research.

Its purpose is to explore the interaction between filesystem virtualization, relational metadata storage, and content-addressable persistence rather than to replace production filesystems.