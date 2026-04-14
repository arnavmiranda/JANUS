# JANUS Architecture Rules
1. TECH STACK: C++17, CMake, `libfuse3`, `sqlite3` (for metadata), OpenSSL `libcrypto` (for SHA-256).
2. MEMORY & SAFETY: 
   - Zero raw `new` or `delete` calls. Enforce RAII.
   - Use `std::unique_ptr` and `std::shared_ptr` exclusively for dynamic memory.
   - Bridge FUSE C-callbacks to C++ state using `fuse_get_context()->private_data`.
3. INVARIANTS: 
   - Never mutate a data block on disk. The Block Store (CAS) is append-only.
   - All metadata mutations must occur inside SQLite transactions to ensure crash resilience.
4. ERROR HANDLING: Return standard negative POSIX error codes (e.g., `-ENOENT`) from FUSE callbacks. Do not throw C++ exceptions across the FUSE C boundary.