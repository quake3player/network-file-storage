# How to Run Docs++ Distributed File System

## Quick Start

### 1. Build All Components
```bash
# From project root
cd common && make && cd ..
cd name_server && make && cd ..
cd storage_server && make && cd ..
cd client && make && cd ..
```

### 2. Start Servers

**Terminal 1: Name Server**
```bash
cd name_server
./name_server 8000 ../nm_data
```

**Terminal 2: Storage Server**
```bash
cd storage_server
./storage_server localhost 8000 SS1 9002
```

**Terminal 3: Client**
```bash
cd client
./client
# Enter username when prompted
```

### 3. Stop Servers
```bash
# Press Ctrl+C in each terminal
# Or: pkill -f name_server && pkill -f storage_server
```

---

## Basic Commands

### File Operations
```
CREATE <filename>                    # Create new file
READ <filename>                      # Read file content
WRITE <filename> <sentence_num>      # Edit file (interactive mode)
  > <word_idx> <content>             # Add/modify words
  > ETIRW                            # Commit changes
DELETE <filename>                    # Delete file
UNDO <filename>                      # Undo last write
```

### Information Commands
```
INFO <filename>                      # File metadata (size, owner, counts)
LIST                                 # List all registered users
VIEW                                 # Your accessible files
VIEW -a                              # All files in system
VIEW -l                              # Detailed view with metadata
VIEW -al                             # All files with details
```

### Access Control
```
ADDACCESS -R <filename> <user>       # Grant read access
ADDACCESS -W <filename> <user>       # Grant read+write access
REMACCESS <filename> <user>          # Revoke all access
REQUESTACCESS <filename> [mode]      # Ask owner for read/write
LISTREQUESTS [filename]              # Owners list pending requests
APPROVEREQUEST <filename> <user> <mode>  # Grant requested access
DENYREQUEST <filename> <user> <mode>     # Reject request and clear queue
ACLINFO <filename>                   # Show current ACL entries
```

### Checkpoint Commands
```
CHECKPOINT <filename> <tag>          # Snapshot current file state
LISTCHECKPOINTS <filename>           # Show available checkpoints
VIEWCHECKPOINT <filename> <tag>      # Display checkpoint contents
REVERTCHECKPOINT <filename> <tag>    # Restore file from checkpoint
```

### Special Commands
```
STREAM <filename>                    # Stream file word-by-word
EXEC <filename>                      # Execute file as bash script
EXIT                                 # Quit client
```

---

## Example Session

```bash
# Start client and enter username: alice
CREATE myfile.txt
WRITE myfile.txt 1
0 Hello
1 World
2 from
3 Docs++
ETIRW
READ myfile.txt
INFO myfile.txt

# Grant access to another user
ADDACCESS -R myfile.txt bob

# Modify file
WRITE myfile.txt 1
0 Modified
1 content
ETIRW

# Undo changes
UNDO myfile.txt
READ myfile.txt

# Cleanup
DELETE myfile.txt
EXIT
```

## Requesting Access Workflow

1. **Requester:** `REQUESTACCESS <file> [read|write]` to queue a request (stored on disk alongside ACLs).
2. **Owner:** Check queues with `LISTREQUESTS <file>` (or without filename to see all files you own).
3. **Decision:** Use `APPROVEREQUEST` or `DENYREQUEST` to resolve each entry; approvals patch the ACL immediately and persist to disk.
4. **Verification:** Any user can run `ACLINFO <file>` to confirm who currently has read/write access.

## Checkpoint Workflow

1. Create a snapshot via `CHECKPOINT <file> <tag>` after major edits.
2. Enumerate stored tags using `LISTCHECKPOINTS <file>`.
3. Preview historical content with `VIEWCHECKPOINT <file> <tag>`.
4. Restore the live file using `REVERTCHECKPOINT <file> <tag>` (this also records an undo entry for safety).

**Tagging tips:** Use descriptive names such as `v1`, `pre-demo`, or timestamps so teammates know what each checkpoint represents.

---

## Running Automated Tests

```bash
# From project root
./tests/test_commands.sh     # Test all commands
./tests/test_view.sh         # Test VIEW variants
./tests/test_bugs_demo.sh    # Verify bug fixes
```

---

## Multi-Client Testing

### Test Concurrent Access

**Terminal 1 (alice):**
```bash
CREATE shared.txt
WRITE shared.txt 1
0 Shared
1 content
ETIRW
ADDACCESS -W shared.txt bob
```

**Terminal 2 (bob):**
```bash
# In another client instance
READ shared.txt              # Should work
WRITE shared.txt 1           # Can modify
```

### Test Access Control

**Terminal 1 (alice):**
```bash
CREATE private.txt
WRITE private.txt 1
0 Private
1 data
ETIRW
```

**Terminal 2 (bob):**
```bash
READ private.txt             # Should fail: "No access"
```

**Terminal 1 (alice):**
```bash
ADDACCESS -R private.txt bob
```

**Terminal 2 (bob):**
```bash
READ private.txt             # Should now work!
WRITE private.txt 1          # Should fail (read-only)
```

---

## Troubleshooting

### "Connection refused"
**Problem:** Can't connect to Name Server  
**Solution:**
- Check if Name Server is running: `pgrep -f name_server`
- Verify port 8000 is available: `netstat -tuln | grep 8000`
- Check logs: `tail -f nm_data/logs/nm.log`

### "No storage servers available"
**Problem:** Storage Server not registered  
**Solution:**
- Check if Storage Server is running: `pgrep -f storage_server`
- Verify it registered successfully (check Name Server output)
- Restart Storage Server if needed

### "Access denied"
**Problem:** No permission to read/write file  
**Solution:**
- Check file owner: `INFO <filename>`
- Ask owner to grant access: `ADDACCESS -R <filename> <yourname>`

### "Sentence already locked"
**Problem:** Another user is editing that sentence  
**Solution:**
- Wait for other user to finish (send `ETIRW`)
- Or edit a different sentence number

### Clean Start (Reset All Data)
```bash
pkill -f name_server
pkill -f storage_server
rm -rf nm_data/ storage_server/ss_data/
# Restart servers - directories will be auto-created
```

---

## Multiple Storage Servers

To run additional storage servers:

**Terminal 4: Second Storage Server**
```bash
cd storage_server
./storage_server localhost 8000 SS2 9003
```

**Terminal 5: Third Storage Server**
```bash
cd storage_server
./storage_server localhost 8000 SS3 9004
```

**Note:** Currently, CREATE assigns files to the first available storage server. Load balancing is not yet implemented.

---

## Viewing Logs

### Name Server Logs
```bash
tail -f nm_data/logs/nm.log
```

### Storage Server Logs
```bash
tail -f storage_server/ss_data/SS1/ss_data_SS1/logs/*.log
```

### Check for Errors
```bash
grep -i error nm_data/logs/nm.log
```

---

## Architecture Overview

```
Client (random port)
    ↓ TCP
Name Server (port 8000) - File registry, ACL, routing
    ↓ Returns SS location
Client → Storage Server (port 9002+) - Direct file I/O
```

**Data Locations:**
- Name Server: `nm_data/` (registry, ACL, logs)
- Storage Server: `storage_server/ss_data/{SS_ID}/` (files, metadata, undo)

---

## For More Information

- **Architecture Details**: `docs/ARCHITECTURE.md`
- **Testing Guide**: `docs/TESTING.md`
- **Project Structure**: `docs/PROJECT_STRUCTURE.md`
- **Course Q&A**: `docs/COURSE_QA.md`
- **Bug Fixes**: `docs/FIXES_APPLIED.md`
