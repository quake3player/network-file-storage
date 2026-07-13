# Project Structure Documentation

## Overview
This document explains every file and folder in the Docs++ distributed file system project.

---

## 📁 Root Level Files

### Core Files
- **`README.md`** - Main project overview and getting started guide
- **`course_project.md`** - Original course project specifications from instructor

### Shell Scripts (Server Management)
- **`stop_servers.sh`** - Script to stop all running servers

### Source Code Directories
- **`client/`** - Client application source code (commands.c, main.c)
- **`name_server/`** - Name Server source code (coordinator, file index, ACL)
- **`storage_server/`** - Storage Server source code (file storage, I/O operations)
- **`common/`** - Shared code used by all components (networking, protocol, persistence)

---

## 📁 `docs/` - Documentation (All Essential)

**All documentation is now organized here - nothing ignored by git!**

- **`ARCHITECTURE.md`** - Complete system architecture, multi-client setup, network diagrams, data structures (Trie)
- **`ARCHITECTURE.md`** - Complete system architecture, multi-client setup, network diagrams, data structures (Trie)
- **`REPORT.md`** - Comprehensive project report (run guide, scripts, code flow, spec mapping). See this for a single-page overview and troubleshooting tips.
- **`COURSE_QA.md`** - Course project Q&A from instructor (important reference)
- **`FIXES_APPLIED.md`** - Detailed bug fixes and improvements log
- **`HOWTO_RUN.md`** - **Start here!** Complete guide to build, run, and test the system
- **`PROJECT_STRUCTURE.md`** - This file - project organization and file guide
- **`TESTING_GUIDE.md`** - Testing guide with automated and manual test cases

**All 6 docs are committed to git and kept up-to-date.**

---

## 📁 `tests/` - Test Scripts

All automated test scripts. See `tests/README.md` for details.

- **`test_commands.sh`** - Comprehensive command testing (CREATE, WRITE, READ, UNDO, etc.)
- **`test_bugs_demo.sh`** - Bug demonstration and fixes verification
- **`test_view.sh`** - VIEW command testing (all flags)
- **`test_phase3.sh`** - Low-level integration tests with Python sockets
- **`README.md`** - Test suite documentation

**Usage:** Run from project root: `./tests/test_commands.sh`

**All scripts use relative paths and work from any directory.**

---

## 📁 `nm_data/` - Name Server Runtime Data

**Purpose:** Persistent storage for Name Server (registry, ACL, logs)

### Subdirectories
- **`registry.jsonl`** - File registry (filename → owner, storage servers, metadata)
- **`acl/`** - Access Control Lists per file (e.g., `abc.acl.json`)
  - Contains permission entries: which users can read/write each file
  - ⚠️ **Many test files** - Consider cleanup: `a.acl.json`, `a1.acl.json`, `aaaaa.acl.json`, etc.
- **`cache/`** - LRU cache data (currently empty - runtime only)
- **`logs/`** - Name Server logs organized by date
  - `nm.log` - Current session log
  - `nm_YYYYMMDD.log` - Historical logs (20251113, 20251114, 20251115, 20251116, 20251118)
  - ⚠️ **Old logs** - Archive or delete logs older than 1 week

---

## 📁 `nm_data_test/` - Name Server Test Data

**Purpose:** Separate data directory for testing (avoid contaminating production data)

- ⚠️ **Can be deleted** if not actively used for automated tests
- Contains same structure as `nm_data/`

---

## 📁 `ss_data/` - Storage Server Runtime Data (ROOT LEVEL)

**Purpose:** ⚠️ **CONFUSING STRUCTURE** - This appears to be leftover from early development

### Current Contents
- `files/`, `logs/`, `metadata/`, `undo/` - Empty directories
- `logs/ss1.log` - Old storage server log
- `SS2/ss_data_SS2/` - Manually created directory for SS2

**Issue:** Storage servers actually use `storage_server/ss_data/{SS_ID}/ss_data_{SS_ID}/`

**Recommendation:** ❌ **DELETE THIS ENTIRE FOLDER** - It's not used by the actual system

---

## 📁 `storage_server/ss_data/` - Actual Storage Server Data

**Purpose:** Runtime data for each storage server instance

### Structure
Each storage server has: `storage_server/ss_data/{SS_ID}/ss_data_{SS_ID}/`

#### Storage Server Instances
- **`SS1/ss_data_SS1/`** - Primary storage server (port 9002)
  - `files/` - Actual file contents (25+ test files like `apple.txt`, `banana.txt`, `test1.txt`)
  - `metadata/` - File metadata JSON (word count, char count, timestamps)
  - `undo/` - Undo history JSON (previous file states)
  - `logs/` - Storage server logs (currently empty)

- **`SS2/ss_data_SS2/`** - Secondary storage server (port 9003) - Empty
- **`SS3/ss_data_SS3/`** - Tertiary storage server (port 9004) - Empty  
- **`SS10/ss_data_SS10/`** - ⚠️ **Test instance** - Can be deleted

**Note:** SS2 and SS3 are empty because the CREATE command currently only uses SS1 (no load balancing implemented yet)

---

## 📁 Component-Specific Folders

### `client/`
- `main.c` - Client entry point and interactive shell
- `include/commands.h` - Command handler function declarations
- `src/commands.c` - Implementation of client commands (CREATE, READ, WRITE, DELETE, etc.)
- `Makefile` - Build configuration for client binary

### `name_server/`
- `src/main.c` - Name Server main logic, command routing, network handling
- `src/file_index.c` - Trie data structure for file indexing (O(m) lookup)
- `include/file_index.h` - File index API and data structures
- `Makefile` - Build configuration for name_server binary

### `storage_server/`
- `src/main.c` - Storage Server main logic, protocol handlers
- `src/storage_engine.c` - File I/O, metadata management, undo functionality
- `include/storage_engine.h` - Storage engine API
- `tests/test_engine.c` - Unit tests for storage engine
- `Makefile` - Build configuration for storage_server binary

### `common/`
- `src/net.c` - Network utilities (send_all, recv_all, socket setup)
- `src/protocol.c` - Binary protocol encoding/decoding
- `src/persistence.c` - JSON persistence for registry and ACL
- `include/*.h` - Header files for shared utilities
- `tests/test_protocol.c` - Unit tests for protocol layer
- `Makefile` - Build configuration for shared library

---

## 🧹 Cleanup Recommendations

### ✅ COMPLETED (Already Done)
1. ✅ Moved all test scripts to `tests/` folder
2. ✅ Moved all documentation to `docs/` folder
3. ✅ Deleted redundant files (FIX_WRITE_ERROR_HANDLING.md, test_all_commands.sh, quick_verify.sh)
4. ✅ Removed empty `scripts/` directory

### 📝 Current .gitignore Strategy

**WHAT IS IGNORED (Runtime/Temporary):**
- PID files (.nm_pid, .ss_pid)
- Build artifacts (*.o, *.a, executables)
- Runtime data directories (nm_data/, storage_server/ss_data/)
- Log files (*.log)
- Test outputs (TEST_RESULTS.txt, ex.txt)
- IDE/OS files (.vscode/, .DS_Store)

**WHAT IS COMMITTED (All Source & Docs):**
- ✅ All .md documentation files (including docs/)
- ✅ All .c and .h source files
- ✅ All .sh test scripts
- ✅ All Makefiles
- ✅ README, course specs

**Key Change:** Removed the aggressive `*.md` ignore rule. All documentation is now properly tracked!

### ⚠️ Optional Runtime Cleanup
```
# Runtime data
nm_data/logs/*.log
nm_data/cache/
storage_server/ss_data/*/ss_data_*/files/*
storage_server/ss_data/*/ss_data_*/logs/*
storage_server/ss_data/*/ss_data_*/metadata/*
storage_server/ss_data/*/ss_data_*/undo/*

# Test outputs
TEST_RESULTS.txt
ex.txt
*.log

# Compiled binaries
client/client
name_server/name_server
storage_server/storage_server
*.o
*.a
```

### 🗂️ KEEP (But cleanup test files)
- `nm_data/acl/` - Delete test ACL files (a.acl.json, a1.acl.json, etc.), keep only active ones
- `storage_server/ss_data/SS1/files/` - Delete test files (a, b, c, e, g, t, t11, te, etc.), keep only meaningful examples

---

## 📊 File Count Summary

**Source Code:** ~15 .c/.h files  
**Documentation:** 10 markdown files (3 redundant)  
**Scripts:** 8 shell scripts  
**Runtime Data:** 100+ test files in SS1  
**Logs:** 6+ log files  

**After Cleanup:** Should reduce to ~80-90 files
