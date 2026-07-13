# Phase 0 Groundwork

Scope: nail down protocol contracts, persistence layout, and shared error vocabulary before writing code. These artifacts form the baseline for Name Server (NM), Storage Server (SS), and Client implementations.

## Messaging Contracts

All TCP exchanges share a fixed header followed by an optional payload. Multibyte integers use network byte order.

```
typedef struct __attribute__((packed)) {
    uint16_t version;      // set to 0x0001 for this project iteration
    uint16_t opcode;       // identifies message type
    uint32_t request_id;   // client-assigned for tracking; NM echoes back
    uint32_t payload_len;  // bytes following the header
} message_header_t;
```

### Opcodes

| Opcode | Hex  | Direction | Meaning |
| ------ | ---- | --------- | ------- |
| OP_REGISTER_SS | 0x0001 | SS → NM | Register storage server |
| OP_REGISTER_CLIENT | 0x0002 | Client → NM | Register client session |
| OP_REGISTER_ACK | 0x8001 | NM → (SS/Client) | Acknowledge registration |
| OP_HEARTBEAT | 0x0003 | SS ↔ NM | Liveness ping with load info |
| OP_LOOKUP_FILE | 0x0004 | Client → NM | Request file location or metadata |
| OP_LOOKUP_RESP | 0x8004 | NM → Client | Response containing routing info |
| OP_COMMAND_FORWARD | 0x0005 | NM → SS | Forward create/delete/access command |
| OP_COMMAND_STATUS | 0x8005 | SS → NM | Status of forwarded command |
| OP_DATA_REQUEST | 0x0006 | Client → SS | Begin READ/STREAM/WRITE session |
| OP_DATA_CHUNK | 0x8006 | SS → Client | Payload chunk during READ/STREAM |
| OP_DATA_ACK | 0x0007 | Client ↔ SS | Confirm chunk receipt / lock grant |
| OP_STOP | 0x7FFF | Any | Explicit end-of-session marker |
| OP_ERROR | 0xFFFF | Any | Structured error response |

### Payload Formats

Payloads are UTF-8 JSON strings with the following schemas (keys in snake_case):

- **REGISTER_SS**
  ```json
  {
    "ss_id": "uuid4",
    "nm_ip": "192.168.1.10",
    "nm_port": 5000,
    "client_port": 5001,
    "capacity_bytes": 268435456,
    "active_load": {"readers": 0, "writers": 0},
    "file_manifest": [
      {"name": "foo.txt", "owner": "alice", "bytes": 1024, "last_modified": "2025-11-08T10:20:00Z"}
    ]
  }
  ```
  NM persists manifest and associates SS.

- **REGISTER_CLIENT**
  ```json
  {
    "username": "alice",
    "client_ip": "192.168.1.50",
    "client_port": 42000
  }
  ```

- **REGISTER_ACK**
  ```json
  {
    "status": "OK",
    "session_token": "uuid4",
    "heartbeat_interval_ms": 5000
  }
  ```

- **HEARTBEAT**
  ```json
  {
    "ss_id": "uuid4",
    "active_load": {"readers": 2, "writers": 1},
    "free_bytes": 134217728
  }
  ```

- **LOOKUP_FILE**
  ```json
  {
    "username": "alice",
    "command": "READ",
    "filename": "foo.txt",
    "flags": ["-l"]
  }
  ```

- **LOOKUP_RESP**
  ```json
  {
    "status": "OK",
    "routing": {
      "ss_id": "uuid4",
      "ip": "192.168.1.11",
      "port": 5001
    },
    "metadata": {"words": 120, "chars": 640, "owner": "alice"}
  }
  ```

- **COMMAND_FORWARD**
  ```json
  {
    "operation": "CREATE",
    "filename": "foo.txt",
    "owner": "alice",
    "acl_patch": {"alice": "RW"}
  }
  ```

- **COMMAND_STATUS**
  ```json
  {
    "operation": "CREATE",
    "status": "OK",
    "undo_token": "uuid4",
    "details": "Created on disk"
  }
  ```

- **DATA_REQUEST** (WRITE example)
  ```json
  {
    "session": "uuid4",
    "username": "alice",
    "filename": "foo.txt",
    "mode": "WRITE",
    "sentence_index": 1
  }
  ```

- **DATA_CHUNK** (STREAM/READ)
  ```json
  {
    "sequence": 12,
    "content": "word",
    "sentence_index": 1,
    "final": false
  }
  ```

- **DATA_ACK**
  ```json
  {
    "sequence": 12,
    "status": "OK"
  }
  ```

- **STOP**
  ```json
  {
    "reason": "END_OF_STREAM"
  }
  ```

- **ERROR**
  ```json
  {
    "code": "E_NO_ACCESS",
    "message": "User lacks write permission",
    "details": {}
  }
  ```

## Persistence Layout

### Name Server Filesystem

```
ns_data/
  registry.jsonl          # append-only log of SS and client registrations
  files_index.json        # filename → {primary_ss, replicas[], metadata_ref}
  acl/
    foo.txt.acl.json      # per-file ACL map {user: "R"|"RW"}
  cache/
    lookup.cache          # serialized LRU cache (filename → ss_id)
  logs/
    nm_YYYYMMDD.log       # structured log entries (JSONL)
```

### Storage Server Filesystem

```
ss_data_<ss_id>/
  files/
    foo.txt               # raw text content, UTF-8
  metadata/
    foo.txt.meta.json     # {owner, created_at, modified_at, words, chars, undo_ref}
  undo/
    foo.txt.undo.json     # {undo_token, snapshot_sentences}
  logs/
    ss_YYYYMMDD.log
```

Notes:
- Files are stored as plain text; sentence parsing occurs on load using `.`, `!`, `?` delimiters per specification and doubts clarifications.
- Metadata files cache aggregate statistics to avoid recompute; update atomically via write-rename to preserve durability.
- Undo snapshots store the entire sentence array resulting from the most recent completed `WRITE` command.

## Error / Status Codes

| Code | Scenario | Returned By |
| ---- | -------- | ----------- |
| E_NO_ACCESS | User lacks required permission | NM, SS |
| E_FILE_NOT_FOUND | Requested file absent | NM, SS |
| E_SENTENCE_LOCKED | Target sentence locked by another writer | SS |
| E_INVALID_SENTENCE | Sentence index out of bounds | SS |
| E_INVALID_WORD | Word index invalid per active sentence | SS |
| E_DUPLICATE | File already exists on create | NM |
| E_NOT_OWNER | Operation requires ownership | NM |
| E_UNREGISTERED_USER | Username not known | NM |
| E_STORAGE_DOWN | SS unreachable during routing | NM |
| E_UNDO_EMPTY | No undo snapshot available | SS |
| E_EXECUTION_FAIL | EXEC command failed | NM |
| E_PROTOCOL | Malformed header/payload | Any |

Errors are wrapped in `OP_ERROR` with `payload_len > 0`. `message` strings remain user-readable; `details` can carry structured diagnostics.

## Configuration Constants

| Constant | Value | Purpose |
| -------- | ----- | ------- |
| PROTOCOL_VERSION | 0x0001 | Allows future upgrades |
| HEARTBEAT_INTERVAL_MS | 5000 | Default SS heartbeat cadence |
| HEARTBEAT_TIMEOUT_MS | 15000 | NM marks SS stale beyond this |
| STREAM_DELAY_MS | 100 | SS throttles STREAM per spec |
| RETRY_LIMIT | 3 | Retries for lost ACKs before surfacing `E_STORAGE_DOWN` |

## Testing Approach

1. **Serialization Round-Trip**: Implement unit tests that populate each payload, serialize header + JSON, then deserialize to ensure field integrity. Use malformed payloads to trigger `E_PROTOCOL`.
2. **Contract Fuzzing**: Using `pytest` or C test harness, feed truncated headers, incorrect `payload_len`, and unknown opcodes; assert NM/SS drop connection gracefully and log `E_PROTOCOL`.
3. **Netcat Handshake Drill**: Spin up dummy listeners (e.g., `nc -lk`). Manually send `OP_REGISTER_SS` frames to NM stub and verify parsed registration enters `registry.jsonl`.
4. **Persistence Smoke Test**: Write scripts that populate sample `ns_data` and `ss_data` trees, restart mock process that loads metadata, and confirm invariants (e.g., ACL map matches manifest).
5. **Error Code Table Verification**: Add automated check ensuring every entry in error table maps to a message template; failing tests flag missing handler implementations early.
6. **Sentence Parser Fixture**: Feed text fixtures (including ones from doubts clarifications) into parsing utility to confirm delimiters and indexing semantics prior to WRITE implementation.
