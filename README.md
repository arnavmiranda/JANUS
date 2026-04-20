# JANUS: A Version-Controlled Content-Addressable Filesystem in Userspace

## Abstract
JANUS is a custom user-space filesystem implemented in C++ that converges virtual filesystem (VFS) architecture, relational database theory, and cryptographic deduplication into a cohesive storage engine. By intercepting POSIX system calls via FUSE (Filesystem in Userspace), JANUS abstracts traditional block-device storage, decoupling file metadata from payload data. It introduces intrinsic, block-level data deduplication, ACID-compliant metadata persistence, and deterministic, Merkle-tree-inspired version control directly at the filesystem interface level.

## System Architecture

The architecture of JANUS is strictly divided into three primary sub-systems:

### 1. The FUSE Intercept Layer (VFS)
JANUS utilizes the `libfuse` bridge to intercept kernel-level POSIX operations. Standard system calls—such as `getattr`, `readdir`, `read`, `write`, `create`, and `unlink`—are routed into custom C++ handlers. This allows JANUS to present a standard POSIX-compliant interface to user applications (e.g., `bash`, `cat`, `echo`) while fundamentally altering how data is persisted in the background.

### 2. Relational Metadata Engine (SQLite)
Traditional filesystems rely on complex internal structures (such as B-trees and journaling logs) to manage inodes and directory hierarchies. JANUS delegates metadata management to a relational database (SQLite). 
* **ACID Compliance:** The database operates in Write-Ahead Logging (WAL) mode, guaranteeing atomicity and durability. If a system crash occurs during a filesystem operation, the transaction is safely rolled back, preventing metadata corruption.
* **Schema:** The database maintains relational mappings between logical filenames, standard POSIX permission modes, byte sizes, and their corresponding cryptographic block pointers.

### 3. Content-Addressable Storage (CAS)
File payloads are never stored linearly or redundantly. Upon a `write` operation, the payload is hashed utilizing the OpenSSL SHA-256 algorithm. The resulting 64-character hexadecimal digest becomes the absolute identifier for that data block, which is stored in a centralized `.janus/blocks/` pool. This architectural decision yields implicit, system-wide data deduplication; multiple duplicate files utilize the physical disk footprint of a single payload block.

## Version Control Mechanics

JANUS extends standard storage capabilities by embedding state-tracking directly into the VFS logic.

* **State Serialization (Snapshotting):** Invoking a commit generates a Merkle-tree-inspired text manifest. This manifest captures the absolute state of the filesystem at a given timestamp by mapping current active inodes to their respective CAS block hashes.
* **Temporal Restoration (Checkout):** To restore a previous state, JANUS executes a destructive teardown of the current metadata tables and reconstructs the SQLite state by deserializing a historical manifest.
* **State Comparison (Diff Engine):** JANUS can parse and compare multiple manifests in memory to deterministically identify file additions, deletions, and modifications without requiring the filesystem to be mounted.
* **Dynamic State Preservation (`.janusignore`):** JANUS supports a robust ignore engine. Files matching patterns within a `.janusignore` file are excluded from manifests. During a destructive temporal restoration (`checkout`), JANUS injects these ignored files into active memory, wipes the database, restores the past state, and securely re-injects the ignored files into the new timeline, ensuring local configuration or secrets are not inadvertently destroyed.

## Advanced Interfaces

* **Interactive Telemetry:** The `janus log` command implements a native C++ terminal user interface (TUI) for navigating chronological filesystem snapshots and executing restorations interactively.
* **Data APIs:** The `janus stats --json` parameter provides machine-readable telemetry regarding total block counts, inode usage, and deduplication efficiency.
* **Native Autocompletion:** JANUS includes a dynamically generated Bash completion script that queries the SQLite database in real-time to suggest available commands and cryptographic commit hashes.

---

## Prerequisites and Compilation

This project requires a Linux environment with standard C++ compilation toolchains and specific development libraries.

**Dependencies:**
```bash
sudo apt update
sudo apt install -y build-essential cmake libfuse3-dev libsqlite3-dev libssl-dev pkg-config
```

**Build Process:**
```bash
mkdir build && cd build
cmake ..
make
```

---

## Operational Lifecycle and Demonstration

To observe the filesystem mechanics, execution requires two distinct terminal sessions: one to host the blocking FUSE daemon, and one to execute standard POSIX and JANUS-specific commands.

### 1. Mounting the Virtual Filesystem (Terminal A)
Initialize the daemon and attach it to an arbitrary mount point.
```bash
mkdir -p /tmp/mt
./janus mount /tmp/mt
```

### 2. State Instantiation and Snapshotting (Terminal B)
Navigate to the compiled binary directory, configure preservation parameters, and establish the initial filesystem state.
```bash
cd build

# Establish Dynamic Preservation Rules
echo "*.log" > /tmp/mt/.janusignore
echo "secret.key" >> /tmp/mt/.janusignore

# Instantiate Files
echo "Primary Application Logic" > /tmp/mt/main.cpp
echo "Diagnostic Output" > /tmp/mt/debug.log

# Serialize State (Commit)
./janus commit -m "Initial system state"
# Note the resulting SHA-256 Hash 1
```

### 3. State Comparison and Analysis (Terminal B)
Simulate subsequent filesystem mutations and utilize the in-memory Diff engine.
```bash
# Mutate the Filesystem
echo "Secondary Application Logic" > /tmp/mt/module.cpp

# Serialize State (Commit)
./janus commit -m "Added secondary module"
# Note the resulting SHA-256 Hash 2

# Compare the Timelines
./janus diff <HASH_1> <HASH_2>
# Expected Output: [+] Added: module.cpp (Note: debug.log is inherently ignored)
```

### 4. Temporal Restoration (Terminal B)
Demonstrate the rollback mechanics while proving the efficacy of the dynamic state preservation engine (`.janusignore`).
```bash
# Instantiate a restricted file matching ignore rules
echo "RESTRICTED_KEY=alpha_numeric" > /tmp/mt/secret.key

# Execute Temporal Restoration to State 1
./janus checkout <HASH_1>
```

**Verification:** Upon restarting the daemon in Terminal A, executing `ls -a /tmp/mt` in Terminal B will yield the following deterministic outcome:
* `module.cpp` has been successfully purged from the metadata tables.
* `main.cpp` has been restored to its original state.
* `.janusignore`, `debug.log`, and `secret.key` have safely bypassed the database wipe and persist in the current working directory.

---

## Repository Structure

```text
janus/
├── src/
│   ├── main.cpp         # Command-line router and FUSE initialization
│   ├── JanusFS.cpp      # POSIX-to-C++ FUSE intercept handlers
│   ├── Database.cpp     # SQLite ACID engine, diffing, and state management
│   ├── BlockStore.cpp   # OpenSSL SHA-256 CAS engine
│   └── *.h              # Header declarations
├── CMakeLists.txt       # Build system configuration
├── janus-completion.bash# Interactive shell autocompletion script
└── README.md
```

## System Teardown
To safely unmount the filesystem and purge all associated databases and cryptographic block stores:
```bash
sudo umount -l /tmp/mt
rm -rf build/janus_meta.db* build/.janus/
```