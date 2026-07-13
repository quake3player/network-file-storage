# Implementation Flow

## Phase 0: Groundwork
- Define message schemas for NM↔SS, NM↔Client, and Client↔SS handshakes, including STOP packet semantics and error codes.
- Select storage layout (on-disk directory tree, metadata files) and decide persistence format for files, ACLs, and undo logs.
- Testing: unit tests for message parsing/serialization; integration smoke test using netcat or mock sockets to verify handshakes and STOP handling.

## Phase 1: Networking Spine
- Implement NM listener accepting SS registrations and client sessions; maintain in-memory registry keyed by file name.
- Build SS listener with dual ports (NM control, client data) and heartbeat/ACK routines per logging requirement.
- Develop client bootstrap capturing username and registering with NM.
- Testing: local loopback tests to register mock SS and clients; simulate disconnect/reconnect; verify NM logs and registry updates.

## Phase 2: Storage Server Core
- Implement lazy file loader that maps sentences→words with delimiter handling; ensure ASCII segmentation logic follows spec and doubts clarifications.
- Add write locks at sentence granularity plus undo log (single-level) and persistent metadata updates per operation.
- Provide INFO metadata computation (size, counts, timestamps, owner, ACL snapshot) stored persistently.
- Testing: unit tests for sentence parser and index updates; concurrency tests with multiple client threads; persistence reboot test restoring state from disk.

## Phase 3: Name Server Services
- Implement efficient search (Trie/HashMap hybrid with LRU cache) for filename→SS lookups, honoring O(1) cached hits and logging every request/response.
- Add access control checks (read/write flags, owner inference) before routing operations; enforce error codes for unauthorized access or locks.
- Provide LIST, VIEW, and INFO aggregations at NM using registry metadata and cached statistics.
- Testing: benchmark lookup latency with warm cache; unit tests for ACL enforcement; integration tests of VIEW/INFO output against sample data.

## Phase 4: Client Command Shell
- Build command parser supporting flag combinations (`VIEW`, `VIEW -a`, `VIEW -al`, etc.) and interactive `WRITE … ETIRW` session with server lock coordination.
- Implement READ, STREAM (word-by-word with 0.1s delay), DELETE (with NM confirmation), UNDO, INFO, LIST, ADDACCESS/REMACCESS, and EXEC flows.
- Ensure EXEC fetches script from SS via NM, executes on NM, and streams stdout/stderr back to client.
- Testing: CLI unit tests for command parsing; integration tests per command using scripted interactions; streaming resilience test where SS drops mid-stream.

## Phase 5: Cross-Cutting Concerns
- Centralize logging for NM and SS with timestamps, IP/port, user, request ID; store logs persistently.
- Implement retry logic for ACK loss and graceful timeout handling across all sockets.
- Add caching invalidation on file create/delete/move and ensure NM broadcasts updates to affected SS replicas if implemented.
- Testing: failure-injection tests for dropped ACKs and socket timeouts; verify log completeness via automated log diff; ACL mutation regression tests.

## Phase 6: Extended Features (Optional Bonus)
- Design folder hierarchy layer with persistent directory metadata and path-aware VIEW commands.
- Implement checkpoints (file-level snapshots tagged and stored persistently) and optional replication strategy with async propagation.
- Testing: folder navigation tests; checkpoint create/revert cycles; replication failover drills by toggling SS availability.
