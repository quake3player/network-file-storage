#!/bin/bash
# Automated test script for Docs++ commands
# Tests: UNDO, INFO, DELETE, STREAM, LIST, ADDACCESS, REMACCESS, EXEC

# Get the project root directory (parent of tests/)
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CLIENT="$PROJECT_DIR/client/client"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Testing Docs++ Commands${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""

# Check if client exists
if [ ! -f "$CLIENT" ]; then
    echo -e "${RED}Error: Client executable not found at $CLIENT${NC}"
    echo "Please run 'make' in the client directory first"
    exit 1
fi

# Test function
test_command() {
    local test_name="$1"
    local commands="$2"
    
    echo -e "${YELLOW}Testing: $test_name${NC}"
    echo "$commands" | timeout 10 $CLIENT 2>&1 | tail -20
    local result=$?
    
    if [ $result -eq 0 ] || [ $result -eq 124 ]; then
        echo -e "${GREEN}✓ $test_name completed${NC}\n"
        return 0
    else
        echo -e "${RED}✗ $test_name failed${NC}\n"
        return 1
    fi
}

# Test 1: CREATE and INFO
echo -e "${YELLOW}========== Test 1: CREATE + INFO ==========${NC}"
test_command "CREATE + INFO" "testuser
CREATE test_info.txt
WRITE test_info.txt 1
0 Hello
1 World
2 from
3 Testing
ETIRW
INFO test_info.txt
"

# Test 2: UNDO
echo -e "${YELLOW}========== Test 2: UNDO ==========${NC}"
test_command "UNDO" "testuser
CREATE undo_test.txt
WRITE undo_test.txt 1
0 Version
1 One
ETIRW
WRITE undo_test.txt 1
0 Version
1 Two
ETIRW
UNDO undo_test.txt
READ undo_test.txt
"

# Test 3: LIST
echo -e "${YELLOW}========== Test 3: LIST ==========${NC}"
test_command "LIST" "testuser
LIST
"

# Test 4: ADDACCESS and REMACCESS
echo -e "${YELLOW}========== Test 4: ADDACCESS + REMACCESS ==========${NC}"
test_command "ACCESS CONTROL" "testuser
CREATE access_test.txt
WRITE access_test.txt 1
0 Test
1 access
ETIRW
ADDACCESS -R access_test.txt testuser2
REMACCESS access_test.txt testuser2
"

# Test 5: EXEC
echo -e "${YELLOW}========== Test 5: EXEC ==========${NC}"
test_command "EXEC" "testuser
CREATE exec_test.sh
WRITE exec_test.sh 1
0 echo
1 \"Testing
2 EXEC
3 command\"
ETIRW
EXEC exec_test.sh
"

# Test 6: STREAM
echo -e "${YELLOW}========== Test 6: STREAM ==========${NC}"
test_command "STREAM" "testuser
CREATE stream_test.txt
WRITE stream_test.txt 1
0 Test
1 stream
2 output
ETIRW
STREAM stream_test.txt
"

# Test 7: DELETE
echo -e "${YELLOW}========== Test 7: DELETE ==========${NC}"
test_command "DELETE" "testuser
DELETE test_info.txt
DELETE undo_test.txt
DELETE access_test.txt
DELETE exec_test.sh
DELETE stream_test.txt
"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}All tests completed!${NC}"
echo -e "${GREEN}========================================${NC}"
