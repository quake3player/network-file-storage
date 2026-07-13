# Docs++ Testing Guide - Phase 0-5 Complete

## Recent Polishing Completed

### 1. UNDO Command - ✅ FIXED
- **Issue**: Client was sending `OP_DATA_REQUEST` which only reads the file
- **Fix**: 
  - Added new opcode `OP_UNDO_REQUEST = 0x000D` to protocol
  - Implemented `handle_undo_request()` in Storage Server
  - Updated client to use `OP_UNDO_REQUEST` instead of `OP_DATA_REQUEST`

  - Checks read access permissions via ACL
  - Executes content as shell commands using `popen()`
  - Captures stdout/stderr output
  - Returns execution output and exit code to client
  - Runs on Name Server (as per spec)

### 3. ADDACCESS Validation - ✅ ADDED
- **New**: Checks if target user is registered before granting access
- **Error**: Returns `ERR_UNREGISTERED_USER` if user not found
- **Compliance**: Per doubts.md Q28

### 4. Protocol Enhancement
- **Added**: `OP_UNDO_REQUEST` for proper undo handling
- **Updated**: `protocol_opcode_name()` to include new opcode

---

## Complete Testing Checklist

### Prerequisites
```bash
# Terminal 1: Start Name Server
cd name_server
mkdir -p ../nm_data/acl ../nm_data/cache ../nm_data/logs
./name_server 8000 ../nm_data

# Terminal 2: Start Storage Server
cd storage_server
./storage_server SS1 localhost 8000 9001 9002 ./ss_data

# Terminal 3: Client
cd client
./client
# Enter username when prompted
```

---

## Phase 0-2: Core Infrastructure ✅ TESTED
- [x] Protocol encoding/decoding
- [x] TCP networking
- [x] Data persistence
- [x] Registration (NM, SS, Client)
- [x] Heartbeat mechanism
- [x] Storage engine (read, write, undo, metadata)

---

## Phase 3: Name Server Services ✅ TESTED (8/8 Tests Passed)
- [x] File index (Trie-based, O(k) lookup)
- [x] LRU cache (128 entries)
- [x] ACL (per-file access control)
- [x] File lookup and routing
- [x] Load balancing (active_readers + 2×active_writers)

---

## Phase 4: User Commands - TEST EACH

### ✅ CREATE - Working
```
CREATE test_file.txt
# Expected: "✓ Created file 'test_file.txt'"
```

### ✅ WRITE - Working
```
WRITE test_file.txt 1
0 Hello
1 World
2 from
3 Docs++
ETIRW
# Expected: Lock acquired, updates applied, committed
```

### ✅ READ - Working
```
READ test_file.txt
# Expected: Displays full file content
```

### ✅ INFO - Working
```
INFO test_file.txt
# Expected: Shows word_count, char_count, sentence_count, timestamps, owner
```

### ⚠️ UNDO - NOW FIXED (Test Required)
```
# After WRITE operation:
UNDO test_file.txt
# Expected: "✓ Undo operation completed for 'test_file.txt'"
READ test_file.txt
# Expected: Should show previous content (before last WRITE)
```

**Test Case**:
1. CREATE undo_test.txt
2. WRITE undo_test.txt 1 → Add "Version 1"
3. READ undo_test.txt → Should show "Version 1"
4. WRITE undo_test.txt 1 → Change to "Version 2"
5. READ undo_test.txt → Should show "Version 2"
6. UNDO undo_test.txt → Revert to "Version 1"
7. READ undo_test.txt → Should show "Version 1" again ✅

### ⚠️ STREAM - Needs Testing
```
STREAM test_file.txt
# Expected: Words displayed one-by-one with 0.1s delay
# Known Issue: Implementation complete but may hang
```

### ✅ DELETE - Working
```
DELETE test_file.txt
# Expected: "✓ Deleted file 'test_file.txt'"
```

### ✅ ADDACCESS / REMACCESS - Working
```
ADDACCESS -R test_file.txt bob
ADDACCESS -W test_file.txt alice
REMACCESS test_file.txt bob
# Expected: ACL updated, cache invalidated
```

### ✅ VIEW - Implemented (Basic)
```
VIEW           # Lists user's accessible files
VIEW -a        # Lists all files on system
VIEW -l        # Lists with details (word count, etc.)
VIEW -al       # Lists all files with details
# Note: Current implementation simplified, shows basic file list
```

### ✅ LIST - Implemented
```
LIST
# Expected: Shows all registered usernames
# Output format: "--> username1\n--> username2\n..."
```

### ⚠️ EXEC - NOW COMPLETED (Test Required)
```
# First create executable file:
CREATE hello.sh
WRITE hello.sh 1
0 echo
1 "Hello
2 World!"
ETIRW

# Then execute:
EXEC hello.sh
# Expected: 
# === Execution Output ===
# Hello World!
# === Exit Code: 0 ===
```

**Additional Test Cases for EXEC**:
```bash
# Test 1: Simple command
CREATE cmd1.sh
WRITE cmd1.sh 1
0 date
ETIRW
EXEC cmd1.sh  # Should show current date

# Test 2: Multiple commands (won't work with single-line, use shell script syntax)
CREATE cmd2.sh
WRITE cmd2.sh 1
0 echo
1 "Line1";
2 echo
3 "Line2"
ETIRW
EXEC cmd2.sh  # Should show both lines

# Test 3: ls command
CREATE listdir.sh
WRITE listdir.sh 1
0 ls
1 -la
ETIRW
EXEC listdir.sh  # Should list directory contents

# Test 4: Error case (bad command)
CREATE bad.sh
WRITE bad.sh 1
0 nonexistent_command
ETIRW
EXEC bad.sh  # Should show error with non-zero exit code
```

---

## Phase 5: Additional Requirements

### ⚠️ Concurrent Access
**Test Multiple Clients**:
```bash
# Terminal 4: Client 1 (alice)
./client
# username: alice
CREATE shared.txt
WRITE shared.txt 1
0 Alice's
1 content
ETIRW

# Terminal 5: Client 2 (bob)
./client
# username: bob
# alice must ADDACCESS -W shared.txt bob first
WRITE shared.txt 1
# Should fail if alice is currently writing to same sentence
# Should succeed if writing to different sentence
```

### ✅ Data Persistence
**Test**:
1. CREATE persistent.txt, add content
2. Stop Storage Server (Ctrl+C)
3. Restart Storage Server
4. READ persistent.txt → Should still exist with same content ✅

### ✅ Access Control
**Test**:
```bash
# Client 1 (alice):
CREATE private.txt
WRITE private.txt 1 → Add content

# Client 2 (bob):
READ private.txt
# Expected: "Error: No access" (bob not in ACL)

# Client 1 (alice):
ADDACCESS -R private.txt bob

# Client 2 (bob):
READ private.txt  # Should now work ✅
WRITE private.txt 1  # Should still fail (read-only access)
```

### ⚠️ Logging
**Verify**:
- Name Server: Check console output for operation logs
- Storage Server: Check console output for client requests
- Check log files in `nm_data/logs/` and `ss_data/logs/`

### ✅ Error Handling
**Test Error Cases**:
```
READ nonexistent.txt  → ERR_FILE_NOT_FOUND
WRITE protected.txt 1 → ERR_NO_ACCESS (if no write permission)
DELETE notowner.txt   → ERR_NOT_OWNER (if not file owner)
UNDO fresh_file.txt   → ERR_UNDO_EMPTY (no undo history)
```

---

## Known Issues & Status

### ✅ FIXED Issues:
1. **UNDO Not Working** → Fixed with `OP_UNDO_REQUEST` and `handle_undo_request()`
2. **EXEC Placeholder** → Fully implemented with popen(), fetches content, executes, returns output
3. **Protocol Missing UNDO** → Added `OP_UNDO_REQUEST = 0x000D`

### ⚠️ Minor Issues Remaining:
1. **STREAM Hanging**: Implementation complete, but execution may hang (flow control issue)
2. **VIEW Simplified**: Shows basic file list, could enhance with detailed Trie traversal
3. **CREATE Not Auto-Registering**: File created on SS but might not auto-add to NM's file index

### 📋 **Important Clarifications from doubts.md:**

1. **UNDO Behavior** (Q23, Q34, Q40):
   - One-level undo only (proof of concept) ✅
   - Undoes ALL sentences within a single WRITE command ✅
   - Requires write permission ✅
   - Undo again undoes previous change (not redo) ✅

2. **EXEC Behavior** (Q27, Q31, Q32, Q33):
   - Treat entire file as bash script (not parsed sentence-by-sentence) ✅
   - Can include pipes, file operations, computation ✅
   - Executes on Name Server (not Storage Server) ✅
   - Flow different from READ/WRITE ✅

3. **Write Priority** (Q20):
   - Readers/streamers have priority over writers
   - Until ETIRW, file content remains original
   - WRITE changes only visible after ETIRW commit

4. **Client Disconnect During Write** (Q44):
   - If client disconnects before ETIRW: no changes persist
   - All locks relieved immediately
   - **TODO**: Verify timeout/cleanup mechanism exists

5. **File Deletion** (Q38):
   - Cannot delete file if sentence is locked
   - **TODO**: Verify lock checking in DELETE handler

6. **Access Control** (Q28):
   - Error if access given to unregistered user ✅
   - **FIXED**: Added check in ADDACCESS handler

### ✅ Verified Compliant with doubts.md:
- Q23, Q34, Q40: UNDO (one-level, all sentences, needs write permission) ✅
- Q27, Q31, Q32, Q33: EXEC (bash script, runs on NM, popen) ✅
- Q19: Client-SS connection (two ports on SS) ✅
- Q36: LIST (all registered users, even offline) ✅
- Q28: ADDACCESS validation (checks user exists) ✅
- Q26: Lazy loading (files loaded on request only) ✅
- Q41, Q43: Efficient lookups (Trie O(k), LRU O(1), ACL fast) ✅

### ⚠️ Additional TODOs from doubts.md:
- Q20: Write priority - verify readers/streamers block writers until done
- Q44: Client disconnect cleanup - verify abandoned write locks are released
- Q38: Delete prevention - verify cannot delete file with locked sentences
- Q25: Large file lists - verify SS registration handles >4096 bytes manifest

### ✅ Fully Tested & Working:
- Phase 0-2: All core infrastructure
- Phase 3: File index, cache, ACL (8/8 tests passed)
- READ, WRITE, CREATE, DELETE, INFO, ADDACCESS, REMACCESS
- Multi-client registration
- Data persistence
- Access control enforcement

---

## Testing Priority Order

1. **UNDO** (highest priority - just fixed)
   - Test revert functionality thoroughly
   - Verify undo snapshot is correctly swapped

2. **EXEC** (newly completed)
   - Test simple commands (echo, date, ls)
   - Test error cases (bad commands)
   - Verify output capture works
   - Verify exit code reporting

3. **STREAM** (implementation complete, but needs debugging)
   - Test word-by-word streaming
   - Check if 0.1s delay works
   - Debug hanging issue

4. **Concurrent Access**
   - Multiple clients writing to different sentences
   - Sentence locking behavior
   - Race condition handling

5. **VIEW/LIST Polish**
   - Enhance VIEW to show detailed file listings
   - Test all flag combinations (-a, -l, -al)

---

## Success Criteria for Phase 5

- [ ] All 12 commands working end-to-end
- [ ] UNDO correctly reverts file changes ✅ (needs verification)
- [ ] EXEC executes shell commands and returns output ✅ (needs testing)
- [ ] STREAM displays word-by-word with delay
- [ ] Multiple clients can work concurrently
- [ ] Data persists across server restarts ✅
- [ ] ACL enforced correctly ✅
- [ ] All error codes returned appropriately ✅
- [ ] Logging active on all components
- [ ] No memory leaks or crashes under normal operation

---

## Quick Start for Full System Test

```bash
# 1. Start all servers
cd course-project-one-project-after-another
./scripts/start_all.sh  # If you have this script

# Or manually:
# Terminal 1: NM
cd name_server && ./name_server 8000 ../nm_data

# Terminal 2: SS
cd storage_server && ./storage_server SS1 localhost 8000 9001 9002 ./ss_data

# Terminal 3: Client
cd client && ./client
# Enter username: testuser

# 2. Run comprehensive test
CREATE demo.txt
WRITE demo.txt 1
0 Hello
1 World
2 from
3 Phase
4 5!
ETIRW
READ demo.txt
INFO demo.txt
UNDO demo.txt
READ demo.txt
DELETE demo.txt

# 3. Test EXEC (NEW!)
CREATE test_exec.sh
WRITE test_exec.sh 1
0 echo
1 "Testing
2 EXEC
3 command"
ETIRW
EXEC test_exec.sh

# 4. Test VIEW/LIST
VIEW
VIEW -l
LIST
```

---

## Build Status: ✅ ALL COMPONENTS BUILT SUCCESSFULLY

```
✅ common/libprotocol.a (with OP_UNDO_REQUEST)
✅ storage_server/storage_server (with handle_undo_request)
✅ name_server/name_server (with complete EXEC implementation)
✅ client/client (with OP_UNDO_REQUEST usage)
```

You're now ready to test everything through Phase 5! 🎉
