# Distributed File System Architecture

## Overview

This document explains the complete architecture of our distributed file system, including all components, ports, and how they communicate with each other.

---

## System Components

### 1. Name Server (Coordinator)
**Role:** Central coordinator and metadata manager

**Responsibilities:**
- Maintains file index (which files exist)
- Tracks file locations across storage servers
- Manages access control lists (ACL)
- Routes client requests to appropriate storage servers
- Queues and persists access requests until owners approve/deny
- Acts as the control plane for checkpoint/list/view/revert commands
- Monitors storage server health via heartbeats
- Handles file operations: CREATE, DELETE, LIST, VIEW, ADDACCESS, REMACCESS

**Command:**
```bash
./name_server <port>
```

**Example:**
```bash
./name_server 8000
```
- Listens on port `8000` for connections from clients and storage servers

---

### 2. Storage Server (File Store)
**Role:** Stores and serves actual file data

**Responsibilities:**
- Stores files on disk in `./ss_data/<SS_ID>/` directory
- Handles file operations: READ, WRITE, STREAM, INFO, UNDO, DELETE
- Creates, lists, serves, and reverts checkpoints per file/tag
- Manages file metadata (word count, sentence count, timestamps)
- Implements sentence-level locking for concurrent writes
- Registers with Name Server on startup
- Sends heartbeat signals to Name Server

**Command:**
```bash
./storage_server <nm_host> <nm_port> <ss_id> <client_port>
```

**Example:**
```bash
./storage_server localhost 8000 SS1 9002
```

**Parameters:**
- `<nm_host>`: Name Server hostname/IP (e.g., `localhost`, `192.168.1.10`)
- `<nm_port>`: Name Server port (e.g., `8000`)
- `<ss_id>`: Unique Storage Server ID (e.g., `SS1`, `SS2`, `SS3`)
- `<client_port>`: Port where this SS listens for client file operations (e.g., `9002`, `9003`, `9004`)

---

### 3. Client (User Interface)
**Role:** Provides command-line interface for users

**Responsibilities:**
- Accepts user commands
- Communicates with Name Server for metadata operations
- Directly connects to Storage Servers for file I/O
- Displays results to user with colored output
- Handles interactive commands: CREATE, READ, WRITE, DELETE, LIST, VIEW, INFO, EXEC, etc.

**Command:**
```bash
./client <nm_host> <nm_port>
```

**Example:**
```bash
./client localhost 8000
```

**Parameters:**
- `<nm_host>`: Name Server hostname/IP
- `<nm_port>`: Name Server port

---

## Network Architecture

### Single Machine Setup (Development/Testing)

```
┌─────────────────────────────────────────────────────────┐
│                     localhost (127.0.0.1)               │
│                                                         │
│  ┌──────────────┐                                      │
│  │ Name Server  │                                      │
│  │  Port: 8000  │                                      │
│  └──────┬───────┘                                      │
│         │                                               │
│  ┌──────┴───────────────────────────┐                 │
│  │              │                    │                  │
│  │              │                    │                  │
│  ▼              ▼                    ▼                  │
│ ┌───────────┐ ┌───────────┐ ┌───────────┐            │
│ │    SS1    │ │    SS2    │ │    SS3    │            │
│ │Port: 9002 │ │Port: 9003 │ │Port: 9004 │            │
│ └─────▲─────┘ └─────▲─────┘ └─────▲─────┘            │
│       │             │             │                     │
│       └─────────────┴─────────────┘                    │
│                     │                                   │
│  ┌──────────────────┴──────────────────┐              │
│  │                 │                    │               │
│  ▼                 ▼                    ▼               │
│ ┌──────────┐ ┌──────────┐ ┌──────────┐               │
│ │ Client1  │ │ Client2  │ │ Client3  │               │
│ │  alice   │ │   bob    │ │  charlie │               │
│ └──────────┘ └──────────┘ └──────────┘               │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

**Startup Commands:**
```bash
# Terminal 1: Start Name Server
./name_server 8000

# Terminal 2: Start Storage Server 1
./storage_server localhost 8000 SS1 9002

# Terminal 3: Start Storage Server 2
./storage_server localhost 8000 SS2 9003

# Terminal 4: Start Storage Server 3
./storage_server localhost 8000 SS3 9004

# Terminal 5: Client 1 (User: alice)
./client localhost 8000

# Terminal 6: Client 2 (User: bob)
./client localhost 8000

# Terminal 7: Client 3 (User: charlie)
./client localhost 8000
```

---

### Distributed Setup (Production)

```
┌─────────────────────────────────────────────────────────────┐
│                    Network: 192.168.1.0/24                  │
│                                                             │
│  ┌──────────────────────────────┐                          │
│  │   Machine 1: 192.168.1.10    │                          │
│  │  ┌────────────────────────┐  │                          │
│  │  │   Name Server          │  │                          │
│  │  │   Port: 8000           │  │                          │
│  │  └──────────┬─────────────┘  │                          │
│  └─────────────┼─────────────────┘                          │
│                │                                             │
│  ┌─────────────┼─────────────────────────────────────┐     │
│  │             │                                      │      │
│  │             │                                      │      │
│  ▼             ▼                                      ▼      │
│ ┌───────────────────┐  ┌───────────────────┐  ┌──────────────────┐
│ │ Machine 2:        │  │ Machine 3:        │  │ Machine 4:       │
│ │ 192.168.1.20      │  │ 192.168.1.30      │  │ 192.168.1.40     │
│ │ ┌───────────────┐ │  │ ┌───────────────┐ │  │ ┌──────────────┐ │
│ │ │ Storage SS1   │ │  │ │ Storage SS2   │ │  │ │ Storage SS3  │ │
│ │ │ Port: 9002    │ │  │ │ Port: 9003    │ │  │ │ Port: 9004   │ │
│ │ └───────▲───────┘ │  │ └───────▲───────┘ │  │ └──────▲───────┘ │
│ └─────────┼─────────┘  └─────────┼─────────┘  └────────┼─────────┘
│           │                      │                       │
│           └──────────────────────┴───────────────────────┘
│                                  │
│  ┌───────────────────────────────┼──────────────────────────┐
│  │                               │                           │
│  ▼                               ▼                           ▼
│ ┌────────────────┐  ┌────────────────┐  ┌────────────────┐
│ │ Machine 5:     │  │ Machine 6:     │  │ Machine 7:     │
│ │ 192.168.1.50   │  │ 192.168.1.60   │  │ 192.168.1.70   │
│ │ ┌────────────┐ │  │ ┌────────────┐ │  │ ┌────────────┐ │
│ │ │  Client    │ │  │ │  Client    │ │  │ │  Client    │ │
│ │ │   alice    │ │  │ │    bob     │ │  │ │  charlie   │ │
│ │ └────────────┘ │  │ └────────────┘ │  │ └────────────┘ │
│ └────────────────┘  └────────────────┘  └────────────────┘
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**Startup Commands:**
```bash
# Machine 1 (192.168.1.10): Name Server
./name_server 8000

# Machine 2 (192.168.1.20): Storage Server 1
./storage_server 192.168.1.10 8000 SS1 9002

# Machine 3 (192.168.1.30): Storage Server 2
./storage_server 192.168.1.10 8000 SS2 9003

# Machine 4 (192.168.1.40): Storage Server 3
./storage_server 192.168.1.10 8000 SS3 9004

# Machine 5 (192.168.1.50): Client 1
./client 192.168.1.10 8000

# Machine 6 (192.168.1.60): Client 2
./client 192.168.1.10 8000

# Machine 7 (192.168.1.70): Client 3
./client 192.168.1.10 8000
```

---

## Port Assignments

| Component | Port | Purpose | Configurable |
|-----------|------|---------|--------------|
| **Name Server** | 8000 | Listens for clients and storage servers | Yes (command line) |
| **Storage Server 1** | 9002 | Serves files to clients | Yes (command line) |
| **Storage Server 2** | 9003 | Serves files to clients | Yes (command line) |
| **Storage Server 3** | 9004 | Serves files to clients | Yes (command line) |
| **Storage Server N** | 9000+N | Serves files to clients | Yes (command line) |
| **Client** | Random | OS assigns ephemeral port | No (automatic) |

### Port Selection Guidelines

**Name Server:**
- Use well-known port (e.g., `8000`)
- All components must know this port
- Avoid ports < 1024 (requires root)

**Storage Servers:**
- Each SS needs unique port
- Common pattern: `9002`, `9003`, `9004`, ...
- Must be > 1024 for non-root execution

**Clients:**
- No configuration needed
- OS automatically assigns from ephemeral port range (32768-65535)

---

## Communication Flow

### 1. File Creation: `CREATE <filename>`

```
┌──────────┐                  ┌──────────────┐                  ┌────────────────┐
│ Client   │                  │ Name Server  │                  │ Storage Server │
│ (alice)  │                  │              │                  │     (SS1)      │
└────┬─────┘                  └──────┬───────┘                  └───────┬────────┘
     │                               │                                  │
     │  CREATE test.txt              │                                  │
     ├──────────────────────────────►│                                  │
     │                               │                                  │
     │                               │  Register file in index          │
     │                               │  Assign to SS1                   │
     │                               │                                  │
     │                               │  CREATE test.txt                 │
     │                               ├─────────────────────────────────►│
     │                               │                                  │
     │                               │  Create file on disk             │
     │                               │  Initialize metadata             │
     │                               │                                  │
     │                               │◄─────────────────────────────────┤
     │                               │  Success                         │
     │                               │                                  │
     │◄──────────────────────────────┤                                  │
     │  File created successfully    │                                  │
     │                               │                                  │
```

---

### 2. File Read: `READ <filename>`

```
┌──────────┐                  ┌──────────────┐                  ┌────────────────┐
│ Client   │                  │ Name Server  │                  │ Storage Server │
│  (bob)   │                  │              │                  │     (SS1)      │
└────┬─────┘                  └──────┬───────┘                  └───────┬────────┘
     │                               │                                  │
     │  READ test.txt                │                                  │
     ├──────────────────────────────►│                                  │
     │                               │                                  │
     │                               │  Lookup file location            │
     │                               │  Check ACL permissions           │
     │                               │  Find: test.txt on SS1           │
     │                               │                                  │
     │◄──────────────────────────────┤                                  │
     │  SS location: localhost:9002  │                                  │
     │                               │                                  │
     │  Connect to SS1:9002                                             │
     ├─────────────────────────────────────────────────────────────────►│
     │  READ test.txt                                                   │
     │                                                                  │
     │                                                                  │
     │◄─────────────────────────────────────────────────────────────────┤
     │  File content: "Hello World!"                                    │
     │                                                                  │
```

---

### 3. File Write: `WRITE <filename> <sentence>`

```
┌──────────┐                  ┌──────────────┐                  ┌────────────────┐
│ Client   │                  │ Name Server  │                  │ Storage Server │
│ (alice)  │                  │              │                  │     (SS1)      │
└────┬─────┘                  └──────┬───────┘                  └───────┬────────┘
     │                               │                                  │
     │  WRITE test.txt 1             │                                  │
     ├──────────────────────────────►│                                  │
     │                               │                                  │
     │                               │  Lookup file location            │
     │                               │  Check WRITE permission          │
     │                               │                                  │
     │◄──────────────────────────────┤                                  │
     │  SS location: localhost:9002  │                                  │
     │                               │                                  │
     │  Connect to SS1:9002                                             │
     ├─────────────────────────────────────────────────────────────────►│
     │  WRITE sentence 1                                                │
     │                                                                  │
     │                                                                  │
     │◄─────────────────────────────────────────────────────────────────┤
     │  Lock acquired, word_count=2                                     │
     │                                                                  │
     │  0 NEW WORD                                                      │
     ├─────────────────────────────────────────────────────────────────►│
     │                                                                  │
     │  1 UPDATED WORD                                                  │
     ├─────────────────────────────────────────────────────────────────►│
     │                                                                  │
     │  ETIRW (commit)                                                  │
     ├─────────────────────────────────────────────────────────────────►│
     │                                                                  │
     │                                                                  │
     │◄─────────────────────────────────────────────────────────────────┤
     │  Changes committed, undo_token: abc123                           │
     │                                                                  │
```

---

### 4. Multi-Client Concurrent Access

```
┌──────────┐  ┌──────────┐           ┌──────────────┐           ┌────────────────┐
│ Client1  │  │ Client2  │           │ Name Server  │           │ Storage Server │
│ (alice)  │  │  (bob)   │           │              │           │     (SS1)      │
└────┬─────┘  └────┬─────┘           └──────┬───────┘           └───────┬────────┘
     │             │                        │                           │
     │  WRITE test.txt 1                   │                           │
     ├────────────────────────────────────►│                           │
     │             │                        │                           │
     │             │                        │  Route to SS1             │
     │             │                        │                           │
     │  Connect to SS1                                                  │
     ├─────────────────────────────────────────────────────────────────►│
     │  WRITE sentence 1                                                │
     │             │                                                    │
     │◄─────────────────────────────────────────────────────────────────┤
     │  Lock acquired (sentence 1)                                      │
     │             │                                                    │
     │             │  WRITE test.txt 1      │                           │
     │             ├───────────────────────►│                           │
     │             │                        │  Route to SS1             │
     │             │                        │                           │
     │             │  Connect to SS1                                    │
     │             ├───────────────────────────────────────────────────►│
     │             │  WRITE sentence 1                                  │
     │             │                                                    │
     │             │◄───────────────────────────────────────────────────┤
     │             │  ERROR: Sentence already locked by alice           │
     │             │                                                    │
     │  Make edits │                                                    │
     │  ETIRW      │                                                    │
     ├─────────────────────────────────────────────────────────────────►│
     │             │                                                    │
     │◄─────────────────────────────────────────────────────────────────┤
     │  Committed  │                                                    │
     │             │                                                    │
     │             │  WRITE test.txt 1                                  │
     │             ├───────────────────────────────────────────────────►│
     │             │                                                    │
     │             │◄───────────────────────────────────────────────────┤
     │             │  Lock acquired (sentence 1 now free)               │
     │             │                                                    │
```

**Key Points:**
- ✅ Multiple clients can READ the same file simultaneously
- ✉️ Access requests live on the Name Server disk; owners review them asynchronously
- 💾 Checkpoints are stored on the Storage Server so snapshots survive Name Server restarts

---

### 5. Access Request Workflow (`REQUESTACCESS`, `LISTREQUESTS`, `APPROVEREQUEST`, `DENYREQUEST`)

1. **Requester → Name Server:** Client sends `REQUESTACCESS` with desired mode (read/write). The Name Server validates the user/file and appends the entry to `nm_data/requests/<filename>.txt`.
2. **Owner review:** Owners query `LISTREQUESTS` (optionally per-file) which streams pending entries from disk.
3. **Decision:**
   - `APPROVEREQUEST` applies ACL changes in memory, persists the ACL file (`nm_data/acl/<filename>.acl.json`), and removes the request row.
   - `DENYREQUEST` simply deletes the pending row so the requester can try again later.
4. **Propagation:** Storage servers are not contacted during this flow; they trust the Name Server to enforce permissions on subsequent LOOKUP/WRT commands.

### 6. Checkpoint Lifecycle (`CHECKPOINT`, `LISTCHECKPOINTS`, `VIEWCHECKPOINT`, `REVERTCHECKPOINT`)

1. **CHECKPOINT:** Client → Name Server (OP_COMMAND_FORWARD) → chosen Storage Server. The SS writes a tagged snapshot under `ss_data/<SS_ID>/checkpoints/<file>/<tag>.chk`.
2. **LISTCHECKPOINTS:** Name Server forwards to the same Storage Server, which enumerates tags under the checkpoint directory and returns JSON.
3. **VIEWCHECKPOINT:** Storage Server reads the checkpoint file and returns the captured content (read-only operation).
4. **REVERTCHECKPOINT:** Storage Server swaps the live file with the snapshot, creates an undo backup, and notifies the Name Server so the client receives an ACK.

**Access control enforcement:**
- Name Server requires **write** permission for CHECKPOINT/REVERT and **read** permission for LIST/VIEW.
- Storage Server never makes ACL decisions; it trusts the Name Server's routing.
- ✅ Multiple clients can WRITE different sentences in the same file simultaneously
- ❌ Only ONE client can WRITE to a specific sentence at a time (sentence-level locking)

---

## Multiple Clients Example Session

### Setup
```bash
# Terminal 1: Name Server
./name_server 8000

# Terminal 2: Storage Server
./storage_server localhost 8000 SS1 9002

# Terminal 3: Client 1 (alice)
./client localhost 8000

# Terminal 4: Client 2 (bob)
./client localhost 8000

# Terminal 5: Client 3 (charlie)
./client localhost 8000
```

### Scenario: Collaborative Document Editing

**Client 1 (alice):**
```
alice@docs++ > CREATE report.txt
✅ File 'report.txt' created successfully!

alice@docs++ > WRITE report.txt 0
✓ Acquired write lock for 'report.txt' sentence 0
   Current word count: 0
Enter updates in format: <word_index> <content>
Type 'ETIRW' to commit and release lock
> 0 Introduction to distributed systems.
> ETIRW
✅ Changes committed!
   Undo token: a7f8e9d2
```

**Client 2 (bob):**
```
bob@docs++ > READ report.txt
Introduction to distributed systems.

bob@docs++ > ADDACCESS report.txt bob RW
✅ Access granted to 'bob': Read, Write

bob@docs++ > WRITE report.txt 1
✓ Acquired write lock for 'report.txt' sentence 1
   Current word count: 0
Enter updates in format: <word_index> <content>
Type 'ETIRW' to commit and release lock
> 0 Architecture overview and components.
> ETIRW
✅ Changes committed!
```

**Client 3 (charlie):**
```
charlie@docs++ > READ report.txt
Introduction to distributed systems. Architecture overview and components.

charlie@docs++ > WRITE report.txt 2
❌ Error: No write access to file
   Code: ERR_NO_ACCESS

charlie@docs++ > READ report.txt
Introduction to distributed systems. Architecture overview and components.
```

**Client 1 (alice) - Concurrent edit attempt:**
```
alice@docs++ > WRITE report.txt 1
❌ Error: Sentence is already locked by another client
   Code: ERR_SENTENCE_LOCKED

alice@docs++ > WRITE report.txt 2
✓ Acquired write lock for 'report.txt' sentence 2
   Current word count: 0
Enter updates in format: <word_index> <content>
Type 'ETIRW' to commit and release lock
> 0 Implementation details will follow.
> ETIRW
✅ Changes committed!
```

**Final Document (any client can view):**
```
alice@docs++ > READ report.txt
Introduction to distributed systems. Architecture overview and components. Implementation details will follow.
```

---

## Scalability: Adding More Components

### Adding Storage Servers

**Why add more storage servers?**
- Increase storage capacity
- Improve redundancy (file replication)
- Balance load across servers
- Reduce latency (geographic distribution)

**How to add:**
```bash
# Add SS4
./storage_server localhost 8000 SS4 9005

# Add SS5
./storage_server localhost 8000 SS5 9006
```

Files are automatically distributed based on:
- Storage server availability
- Load balancing policies
- Replication requirements

---

### Adding Clients

**Why multiple clients?**
- Support concurrent users
- Enable collaborative editing
- Distribute workload
- Real-world multi-user scenarios

**How to add:**
```bash
# As many clients as needed!
./client localhost 8000  # Client N
./client localhost 8000  # Client N+1
./client localhost 8000  # Client N+2
```

**Each client:**
- Gets unique ephemeral port
- Independent user session
- Can operate concurrently
- Respects file locks and permissions

---

## Performance Characteristics

### Concurrent Operations Support

| Operation | Multiple Clients | Same File | Same Sentence |
|-----------|-----------------|-----------|---------------|
| **READ** | ✅ Unlimited | ✅ Yes | ✅ Yes |
| **CREATE** | ✅ Unlimited | ❌ No | N/A |
| **DELETE** | ✅ Yes (owner only) | N/A | N/A |
| **WRITE** | ✅ Yes | ✅ Yes | ❌ No (locked) |
| **INFO** | ✅ Unlimited | ✅ Yes | ✅ Yes |
| **LIST** | ✅ Unlimited | N/A | N/A |
| **VIEW** | ✅ Unlimited | N/A | N/A |

### Locking Granularity

**File-level operations:**
- CREATE: Exclusive (one client at a time per filename)
- DELETE: Exclusive (owner only)

**Sentence-level operations:**
- WRITE: Exclusive per sentence (multiple clients can edit different sentences)
- Automatic lock release on ETIRW or disconnect

**No locking:**
- READ: Concurrent access allowed
- INFO/LIST/VIEW: Metadata operations, no conflicts

---

## Failure Scenarios

### Client Disconnection
```
Client writes to sentence 1 → Client crashes before ETIRW
Result: Lock automatically released after connection timeout
Other clients can now access sentence 1
```

### Storage Server Failure
```
SS1 goes offline → Name Server detects via missed heartbeats
Name Server marks SS1 as unavailable
Files on SS1 become inaccessible (unless replicated on SS2/SS3)
```

### Name Server Failure
```
Name Server crashes → All clients lose coordination
Storage servers continue running but isolated
System requires Name Server restart to resume operations
```

---

## Summary

### Architecture Benefits

✅ **Scalability**
- Add storage servers for more capacity
- Add clients for more users
- Linear scaling with hardware

✅ **Concurrency**
- Multiple clients work simultaneously
- Sentence-level locking prevents conflicts
- High throughput for read operations

✅ **Fault Tolerance**
- File replication across storage servers
- Graceful degradation on server failure
- Lock timeout prevents deadlocks

✅ **Simplicity**
- Clear separation of concerns
- Easy to deploy and configure
- Minimal configuration required

### Quick Start Commands

**All on one machine:**
```bash
# Start everything
./name_server 8000 &
./storage_server localhost 8000 SS1 9002 &
./storage_server localhost 8000 SS2 9003 &
./client localhost 8000  # Terminal 1
./client localhost 8000  # Terminal 2
./client localhost 8000  # Terminal 3
```

Now multiple users can collaborate on files in real-time! 🚀
