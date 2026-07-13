````markdown
# Docs++ — Comprehensive Report

This document summarizes the distributed file system project: how to run it locally and across machines, what each script does, the overall code flow (which files start what and how requests move through the system), supported features, how the features map to the assignment specification, and short operational guidance.

## Table of contents
- Quick summary
- How to run (single machine + distributed)
- Using multiple terminals and choosing IPs (ifconfig/ip)
- Scripts in `scripts/` and `tests/` — what they do and how to use them
- High-level code flow (client → name server → storage server)
- Key files and responsibilities
- What the system supports (user visible features)
- How we implemented the specification (brief checklist)
- One-line summary of how functionality was achieved
- Next steps and helpful tips

---

## Quick summary

Docs++ is a small, educational distributed file system implemented in C. It uses a central Name Server (NM) for metadata and routing, multiple Storage Servers (SS) for persistent file storage, and CLI Clients for user interactions. Communication uses a compact binary header plus JSON payloads. File data is text-only and split into sentences and words; writes are sentence-locked.

## How to run (single-machine development)

Open one terminal per component. Example commands (run from project root):

```bash
# Terminal 1: start Name Server on port 8000
cd /path/to/project/name_server
./name_server 8000

# Terminal 2: start Storage Server 1 (registers with NM)
cd /path/to/project/storage_server
./storage_server localhost 8000 SS1 9002

# Terminal 3: start Storage Server 2
./storage_server localhost 8000 SS2 9003

# Terminal 4..N: start Clients
cd /path/to/project/client
./client localhost 8000  # will prompt for username
```

Notes:
- Use the full filesystem path to the binaries if you are in a different working directory.
- Start the Name Server first so Storage Servers can register.

## How to run across multiple machines (distributed)

1. On the Name Server machine, find its IP via `ifconfig` or `ip addr`.

Example (Linux):

```bash
# show interfaces
ip addr show

# or older tool
ifconfig -a
```

Look for the IP of the interface you want to use (e.g., `192.168.1.10`).

2. Start the Name Server on that machine:

```bash
./name_server 8000
```

3. On each storage server machine, start storage server pointing to NM IP:

```bash
./storage_server 192.168.1.10 8000 SS1 9002
```

4. On client machines, start the client and point it to NM IP:

```bash
./client 192.168.1.10 8000
```

Network tips:
- If you have multiple NICs, pick the correct interface IP. `ip route get 8.8.8.8` shows the interface used to reach the Internet.
- Ensure firewall allows ports (8000 for NM, 9002+ for SS client ports).

## Running multiple terminals locally (tips)

- Use tmux or GNU screen to manage multiple terminals in one window.
- Example tmux: `tmux new -s docspproject` and `tmux split-window` to open panes. Use one pane for NM, one for SS1, one for SS2, and multiple for clients.

## Scripts and what they do

Project has several helper scripts under `scripts/` and automated tests under `tests/`:

- `scripts/start_all.sh` / `scripts/stop_servers.sh`: convenience scripts to start/stop NM and a set of storage servers (development use). They call binaries with default ports.
- `scripts/rebuild_and_run.sh`: builds the binaries (via each subproject Makefile) and starts a small dev cluster (NM + SS1) plus a client for quick testing.
- `scripts/test_phase3.sh` (or `tests/test_phase3.sh`): low-level integration test that exercises newly added Phase 3 features (file index, lookup, cache). It uses Python or shell socket helpers to simulate client behavior.
- `tests/test_commands.sh`: higher-level tests for CREATE/WRITE/READ/UNDO/DELETE workflows.

How to use:

```bash
# rebuild & run a small local cluster
cd scripts
./rebuild_and_run.sh

# run the phase3 test (integration)
bash ../tests/test_phase3.sh

# run full command tests (may require interactive binaries)
./tests/test_commands.sh
```

Read each script to see environment variables and defaults; they include comments explaining what they start and ports they expect.

## Overall code flow (high-level)

1. Client sends a command to Name Server (e.g., VIEW, CREATE, DELETE, LOOKUP request for READ/WRITE).
2. Name Server handles metadata commands directly (LIST, VIEW folder, ADDACCESS, REMACCESS), or performs a lookup and returns the Storage Server IP/port to the client for read/write/stream.
3. For READ/WRITE/STREAM: the client connects directly to the selected Storage Server and performs protocol exchanges (binary header + JSON payloads). For writes, SS grants sentence locks and accepts a sequence of OP_WRITE_UPDATE messages followed by OP_WRITE_COMMIT.
4. Storage Server persists files under `storage_server/ss_data/<SS_ID>/...`, manages metadata (sentence counts, word counts), stores checkpoints and undo history.

### Key files to look at (start points)

- `client/main.c` — interactive shell, user command parsing
- `client/src/commands.c` — implements commands and protocol client side
- `name_server/src/main.c` — server loop, opcode dispatch, lookup/command handlers
- `name_server/src/file_index.c` — trie + LRU cache + ACL enforcement
- `storage_server/src/main.c` — main network loop and opcode handling; handles OP_WRITE_UPDATE / OP_WRITE_COMMIT and sentence locks
- `storage_server/src/storage_engine.c` — file I/O, persistence, undo/checkpoint
- `common/src/protocol.c` and `common/src/net.c` — low level message framing and socket helpers

## Concurrency and locking

- Sentence-level locking: when a client wants to `WRITE <file> <sentence_idx>`, the SS checks and grants a lock for that sentence only. Other clients can write different sentences concurrently.
- Locking is tracked by the SS; locks are released on ETIRW (commit) or on client disconnect/timeout.
- If two clients attempt to write different sentences, both may acquire locks concurrently. If writes change the sentence structure (e.g., adding sentences), the server adjusts sentence indices carefully; write conflicts that result in invalid sentence indices are rejected with an error. Previously, an error path caused the SS to exit — this has been fixed by ensuring errors return OP_ERROR instead of crashing the server.

## What the system supports (user-visible features)

- VIEW (with -a and -l flags)
- VIEWFOLDER / CREATEFOLDER / MOVEFOLDER (folder features implemented)
- CREATE, READ, WRITE (sentence-level), DELETE
- STREAM (word-by-word streaming with 0.1s delay)
- UNDO (per-file undo stack)
- INFO (file metadata)
- ADDACCESS / REMACCESS (ACL management), LIST users
- EXEC (execute file content on NM and return output)
- CHECKPOINT / VIEWCHECKPOINT / REVERT (checkpointing support)

## How the implementation maps to the specification (short checklist)

- Name Server: central coordinator, file index and ACL enforcement — implemented in `name_server/` (file_index trie + LRU cache)
- Storage Servers: persistent storage, checkpoints, undo — implemented in `storage_server/` (storage_engine)
- Clients: CLI shell, connects to NM and SS — implemented in `client/` (commands.c)
- Sentences and words model: SS splits file into sentences and enforces sentence-level locks — `storage_engine.c`
- Concurrency: sentence-level locks + lock timeouts to avoid deadlocks
- Persistence: NM and SS persist metadata and files to `nm_data/` and `storage_server/ss_data/`
- Logging and errors: NM and SS log operations with timestamps and return standardized JSON error objects

## One-line / two-line summary of how functionality was achieved

- We split responsibilities: NM stores and serves metadata and routing, while SS stores persistent file content and enforces sentence-level locks. Clients interact via a small binary-framed JSON protocol. Efficient lookups are enabled by a trie + LRU cache at the NM; storage durability and undo/checkpoint are handled by the SS filesystem layout.

## Spec checklist (detailed mapping)

- User Clients — yes: `client/` (interactive CLI) supports all listed commands.
- Name Server — yes: `name_server/` maintains mapping, ACLs, and routing.
- Storage Servers — yes: `storage_server/` persists files, provides read/write/stream/undo/checkpoint.
- Sentence-level locking — yes: implemented in `storage_server/src/main.c` and storage engine.
- Concurrency — yes: clients can read concurrently; writes lock sentences.
- Undo — yes: SS stores undo tokens and previous versions in `undo/`.
- Access Control — yes: per-file ACLs in `nm_data/acl` and enforced by NM before routing.
- Checkpoints — yes: stored in SS checkpoint directories.
- EXEC — yes: executed on NM (careful: this runs commands and returns output; use trusted test files only).

## Troubleshooting & common issues

- If SS crashes on write: check the server log under `storage_server/ss_data/<SS_ID>/logs/` and ensure NM is reachable.
- If clients see truncated writes with quotes: ensure both client and server are on the latest build (we added robust JSON escaping/unescaping to avoid that).
- If locks are not released after a client kills: restart the SS or wait for lock timeout (configurable in `storage_server/src/main.c`).

## Next steps / improvements

- Add unit tests for JSON parsing and socket handling.
- Add replication (async write to replicas) for higher availability.
- Add monitoring and Prometheus metrics endpoints for NM/SS.
- Harden EXEC handling: sandbox execution or restrict to trusted users.

---

If you want, I can now:

1. Update `docs/PROJECT_STRUCTURE.md` with a short link and summary pointing to this report (I will do that now). 
2. Run an end-to-end test locally (start NM, SS, client) and show the WRITE/READ fix (requires the ability to run the binaries in your workspace). 

Which of these would you like next?

````