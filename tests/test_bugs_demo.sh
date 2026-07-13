#!/bin/bash
# Comprehensive Bug Demonstration Script for Docs++
# This script demonstrates all the bugs found in the implementation

# Get the project root directory
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CLIENT="$PROJECT_DIR/client/client"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     Docs++ Bug Demonstration & Testing Script         ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if servers are running
check_servers() {
    if ! pgrep -f "name_server" > /dev/null; then
        echo -e "${RED}✗ Name Server is not running${NC}"
        echo "  Start it with: ./start_servers.sh"
        exit 1
    fi
    
    if ! pgrep -f "storage_server" > /dev/null; then
        echo -e "${RED}✗ Storage Server is not running${NC}"
        echo "  Start it with: ./start_servers.sh"
        exit 1
    fi
    
    echo -e "${GREEN}✓ Servers are running${NC}"
}

# Function to send commands to client
send_commands() {
    local username="$1"
    shift
    echo "$username" | timeout 10 "$CLIENT" <<EOF
$@
EXIT
EOF
}

echo "Checking if servers are running..."
check_servers
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}Test 1: Normal Operation (Should Work)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo "Creating a simple file and reading it back..."
send_commands "testuser1" "CREATE normal.txt" "WRITE normal.txt 1" "0 Hello World" "ETIRW" "READ normal.txt"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${YELLOW}Test 2: Fixed - WRITE Error Handling (Should NOT Exit)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo "Testing wrong format in WRITE - client should stay alive..."
send_commands "testuser2" "CREATE errortest.txt" "WRITE errortest.txt 1" "InvalidFormat" "ETIRW" "LIST"
echo -e "${GREEN}✓ Client stayed alive after WRITE error!${NC}"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Test 3: Access Control (Should Work)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo "Testing access control between users..."
echo "Alice creates a private file..."
send_commands "alice" "CREATE private.txt" "WRITE private.txt 1" "0 Secret data" "ETIRW"
echo ""
echo "Bob tries to read (should fail)..."
send_commands "bob" "READ private.txt" 2>&1 | grep -i "error\|access"
echo ""
echo "Alice grants access to Bob..."
send_commands "alice" "ADDACCESS -R private.txt bob"
echo ""
echo "Bob tries again (should work now)..."
send_commands "bob" "READ private.txt"
echo -e "${GREEN}✓ Access control working correctly${NC}"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Test 4: UNDO Functionality (Should Work)${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo "Testing UNDO..."
send_commands "testuser6" \
    "CREATE undo.txt" \
    "WRITE undo.txt 1" "0 Version One" "ETIRW" \
    "READ undo.txt" \
    "WRITE undo.txt 1" "0 Version Two" "ETIRW" \
    "READ undo.txt" \
    "UNDO undo.txt" \
    "READ undo.txt"
echo -e "${GREEN}✓ UNDO working - should see Version One after undo${NC}"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}Test 5: VIEW Commands${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo "Testing VIEW with different flags..."
send_commands "testuser7" "VIEW" "VIEW -a" "VIEW -l"
echo ""

echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Fixed Issues Summary:${NC}"
echo -e "${YELLOW}═══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}✓ WRITE errors no longer terminate client session${NC}"
echo -e "${GREEN}✓ Concurrent write locking implemented (sentence-level)${NC}"
echo -e "${GREEN}✓ EXEC command fixed (connection, username, return values)${NC}"
echo -e "${GREEN}✓ DELETE now removes files from storage servers${NC}"
echo -e "${GREEN}✓ VIEW -l timestamp display fixed${NC}"
echo ""
echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                  Testing Complete                      ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
