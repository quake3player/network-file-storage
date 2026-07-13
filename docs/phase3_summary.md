# Phase 3 Implementation Summary

## Completed: Name Server Services (Phase 3)

### What Was Implemented

#### 1. **Efficient File Index (O(1) lookup)**
- **Data Structure**: Trie-based index for filename → storage server mapping
- **Implementation**: `name_server/src/file_index.c` and `name_server/include/file_index.h`
- **Key Features**:
  - Trie allows O(1) average-case lookup by filename
  - Each file record tracks: filename, owner, storage servers (up to 8 replicas), ACL entries, metadata
  - Supports multiple storage servers per file for redundancy

#### 2. **LRU Cache (O(1) cached lookups)**
- **Implementation**: Doubly-linked list + hash table simulation
- **Capacity**: 128 entries (configurable)
- **Key Features**:
  - Cache hit → move to front (MRU position)
  - Cache miss → fetch from file index, add to cache, evict LRU if at capacity
  - Cache invalidation on CREATE/DELETE/ADDACCESS/REMACCESS operations

#### 3. **Access Control (ACL)**
- **Per-file ACL**: Each file maintains a list of (username, can_read, can_write) entries
- **Owner always has full access**: Owner check bypasses ACL
- **Operations**:
  - `ADDACCESS`: Owner can grant read or write (implies read) access to users
  - `REMACCESS`: Owner can revoke all access from users
  - Lookup enforces ACL before returning SS routing info

#### 4. **File Lookup Handler (OP_LOOKUP_FILE)**
- **Flow**:
  1. Parse request: filename, username, operation (read/write)
  2. Check LRU cache first (O(1) if cached)
  3. If miss, query file index (O(k) where k = filename length)
  4. Enforce ACL: check if user has required access (read or write)
  5. Select best storage server: choose SS with lowest active load (readers + 2×writers)
  6. Return routing info: `{ss_ip, ss_port, owner}`
- **Logging**: Every lookup is logged with timestamp, username, filename, and result

#### 5. **Command Forwarding (OP_COMMAND_FORWARD)**
Handles the following commands:
- **CREATE**: Add new file to index, assign to SS, set owner
- **DELETE**: Remove file from index (owner-only), invalidate cache
- **ADDACCESS**: Add ACL entry (owner-only), invalidate cache
- **REMACCESS**: Remove ACL entry (owner-only), invalidate cache

#### 6. **Storage Server Registration Enhanced**
- Now parses `file_manifest` JSON array from SS registration payload
- Populates file index with all files reported by each SS
- Supports multiple SSs registering with same files (replication)

### Test Results

```
✓ File index (Trie-based) initialized
✓ LRU cache (capacity 128) initialized
✓ SS registration with file manifest: 3 files registered
✓ Client registration: alice
✓ File lookup (cache miss): doc1.txt → routed to SS test-ss-1
✓ File lookup (cache hit): doc1.txt → cached response
✓ CREATE command: newfile.txt created by alice
✓ Lookup for non-existent file: ERR_FILE_NOT_FOUND returned
✓ All operations logged with timestamps
```

### Files Modified/Created

**New Files**:
- `name_server/include/file_index.h` - File index and cache API
- `name_server/src/file_index.c` - Trie, ACL, and LRU cache implementation
- `scripts/test_phase3.sh` - Integration test script

**Modified Files**:
- `name_server/src/main.c`:
  - Added `file_index` and `cache` to `nm_state_t`
  - Enhanced `handle_register_ss` to parse file manifest
  - Added `handle_lookup_file` for OP_LOOKUP_FILE
  - Added `handle_command_forward` for CREATE/DELETE/ADDACCESS/REMACCESS
  - Updated `handle_payload` to dispatch new opcodes
  - Added file index/cache init and cleanup in `main()`
- `name_server/Makefile`:
  - Added `src/file_index.c` to sources
  - Added `-Iinclude` to CFLAGS

### Performance Characteristics

- **File lookup**: O(1) cached, O(k) uncached (k = filename length)
- **ACL check**: O(a) where a = ACL entry count per file
- **SS selection**: O(n) where n = replica count (max 8)
- **Cache eviction**: O(1) LRU eviction

### Next Steps (Phase 4 & Beyond)

Phase 3 provides the foundation for:
- **Phase 4**: Client command shell can now use OP_LOOKUP_FILE to locate files and OP_COMMAND_FORWARD for CREATE/DELETE/access control
- **LIST/INFO operations**: Can iterate file index to list all files, filter by ACL
- **Persistence**: ACL and file index can be saved/restored from `ns_data/files_index.json`
- **Replication**: Multiple SSs per file already supported in data structures

### Build & Run

```bash
# Build
cd name_server && make

# Run
./name_server <port> <data-dir>

# Test
bash ../scripts/test_phase3.sh
```

---

**Status**: Phase 3 COMPLETE ✓  
**Build**: PASS ✓  
**Tests**: PASS ✓  
**Next**: Phase 4 (Client Command Shell)
