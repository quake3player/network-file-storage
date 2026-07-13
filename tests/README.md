# Tests Directory

This directory contains all test scripts for the Docs++ distributed file system.

## Test Scripts

### `test_commands.sh`
Comprehensive test suite for all commands:
- CREATE, WRITE, READ, INFO
- UNDO functionality
- LIST command
- ADDACCESS, REMACCESS (access control)
- EXEC command
- STREAM command
- DELETE command

**Usage:**
```bash
./tests/test_commands.sh
```

**Prerequisites:** Servers must be running (use `./start_servers.sh`)

---

### `test_bugs_demo.sh`
Demonstrates bug fixes and system robustness:
- Normal operations verification
- WRITE error handling (client stays alive)
- Access control between multiple users
- UNDO functionality
- VIEW commands

**Usage:**
```bash
./tests/test_bugs_demo.sh
```

**Prerequisites:** Servers must be running

---

### `test_view.sh`
Focused testing of VIEW command variants:
- `VIEW` - User's accessible files only
- `VIEW -a` - All files in system
- `VIEW -l` - With detailed metadata
- `VIEW -al` - All files with details
- Access control verification for VIEW

**Usage:**
```bash
./tests/test_view.sh
```

**Prerequisites:** Servers must be running

---

### `test_phase3.sh`
Low-level integration tests using Python socket programming:
- Storage server registration
- Client registration
- File lookup (cache miss and cache hit)
- CREATE command
- Non-existent file handling
- Log verification

**Usage:**
```bash
./tests/test_phase3.sh
```

**Note:** This test uses a separate test port (5000) and test data directory.

---

## Running Tests

### Quick Test
```bash
# Start servers
./start_servers.sh

# In another terminal, run tests
./tests/test_commands.sh
```

### Full Test Suite
```bash
# Run all tests
./tests/test_commands.sh
./tests/test_bugs_demo.sh
./tests/test_view.sh
./tests/test_phase3.sh
```

### Individual Command Testing
For manual testing, use the client directly:
```bash
cd client
./client
```

---

## Test Output

All test scripts provide colored output:
- 🟢 **Green**: Success
- 🔴 **Red**: Failure/Error
- 🟡 **Yellow**: Information/In Progress
- 🔵 **Blue**: Section Headers

---

## Troubleshooting

**"Client executable not found"**
- Run `make` in the `client/` directory

**"Servers are not running"**
- Start servers with `./start_servers.sh`
- Check server status: `pgrep -f name_server && pgrep -f storage_server`

**"Connection refused"**
- Verify Name Server is listening on port 8000
- Check logs: `tail -f nm_data/logs/nm.log`

**Tests timeout**
- Increase timeout in scripts (default: 10 seconds)
- Check server logs for errors

---

## Adding New Tests

To add a new test script:

1. Create script in `tests/` directory
2. Make it executable: `chmod +x tests/new_test.sh`
3. Use project-relative paths:
   ```bash
   PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
   CLIENT="$PROJECT_DIR/client/client"
   ```
4. Add documentation to this README
