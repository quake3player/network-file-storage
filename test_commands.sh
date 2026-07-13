#!/bin/bash
# Comprehensive test script for all commands
# Tests: INFO, DELETE, UNDO, STREAM, LIST, ADDACCESS, REMACCESS, EXEC

CLIENT_DIR="/home/divlin/Desktop/Anshulz/iiit-coursework/sem-3/OSN/Assignments/course-project-one-project-after-another/client"
TEST_LOG="/tmp/command_test.log"

echo "=== Testing Commands ===" > "$TEST_LOG"

# Function to run client command
run_client_cmd() {
    local username="$1"
    shift
    echo -e "$username\n$@\nEXIT" | "$CLIENT_DIR/client" 2>&1 | tee -a "$TEST_LOG"
}

echo "====================================="
echo "Testing Phase 4-5 Commands"
echo "====================================="

# Test 1: CREATE a file
echo -e "\n[TEST 1] CREATE command"
run_client_cmd "alice" "CREATE test_file.txt"

# Test 2: WRITE to file
echo -e "\n[TEST 2] WRITE command"
run_client_cmd "alice" "WRITE test_file.txt 1" "0 Hello" "1 World" "2 from" "3 Docs++" "ETIRW"

# Test 3: INFO command
echo -e "\n[TEST 3] INFO command"
run_client_cmd "alice" "INFO test_file.txt"

# Test 4: READ to verify content
echo -e "\n[TEST 4] READ command (before UNDO)"
run_client_cmd "alice" "READ test_file.txt"

# Test 5: WRITE again (for UNDO test)
echo -e "\n[TEST 5] WRITE again (Version 2)"
run_client_cmd "alice" "WRITE test_file.txt 1" "0 Modified" "1 content" "ETIRW"

# Test 6: READ (should show modified)
echo -e "\n[TEST 6] READ (after modification)"
run_client_cmd "alice" "READ test_file.txt"

# Test 7: UNDO command
echo -e "\n[TEST 7] UNDO command"
run_client_cmd "alice" "UNDO test_file.txt"

# Test 8: READ (should show original)
echo -e "\n[TEST 8] READ (after UNDO - should be original)"
run_client_cmd "alice" "READ test_file.txt"

# Test 9: LIST command
echo -e "\n[TEST 9] LIST command"
run_client_cmd "alice" "LIST"

# Test 10: Register second user
echo -e "\n[TEST 10] Register second user (bob)"
run_client_cmd "bob" "LIST"

# Test 11: ADDACCESS -R
echo -e "\n[TEST 11] ADDACCESS -R (give bob read access)"
run_client_cmd "alice" "ADDACCESS -R test_file.txt bob"

# Test 12: ADDACCESS -W
echo -e "\n[TEST 12] ADDACCESS -W (give bob write access)"
run_client_cmd "alice" "ADDACCESS -W test_file.txt bob"

# Test 13: Bob tries to READ
echo -e "\n[TEST 13] Bob reads file (should work)"
run_client_cmd "bob" "READ test_file.txt"

# Test 14: REMACCESS
echo -e "\n[TEST 14] REMACCESS (remove bob's access)"
run_client_cmd "alice" "REMACCESS test_file.txt bob"

# Test 15: Bob tries to READ (should fail)
echo -e "\n[TEST 15] Bob reads file (should fail - no access)"
run_client_cmd "bob" "READ test_file.txt"

# Test 16: Re-add access for EXEC test
echo -e "\n[TEST 16] Re-add read access for EXEC test"
run_client_cmd "alice" "ADDACCESS -R test_file.txt bob"

# Test 17: CREATE executable file
echo -e "\n[TEST 17] CREATE executable file"
run_client_cmd "alice" "CREATE hello_script.sh"

# Test 18: WRITE bash commands
echo -e "\n[TEST 18] WRITE bash script content"
run_client_cmd "alice" "WRITE hello_script.sh 1" "0 echo" "1 Hello" "2 from" "3 EXEC!" "ETIRW"

# Test 19: EXEC command
echo -e "\n[TEST 19] EXEC command"
run_client_cmd "alice" "EXEC hello_script.sh"

# Test 20: STREAM command
echo -e "\n[TEST 20] STREAM command (word-by-word)"
timeout 10 bash -c "echo -e 'alice\nSTREAM test_file.txt\nEXIT' | $CLIENT_DIR/client" 2>&1 | tee -a "$TEST_LOG"

# Test 21: DELETE command
echo -e "\n[TEST 21] DELETE command"
run_client_cmd "alice" "DELETE hello_script.sh"

# Test 22: Verify DELETE
echo -e "\n[TEST 22] Try to READ deleted file (should fail)"
run_client_cmd "alice" "READ hello_script.sh"

# Test 23: ADDACCESS to unregistered user (should fail)
echo -e "\n[TEST 23] ADDACCESS to unregistered user (should fail)"
run_client_cmd "alice" "ADDACCESS -R test_file.txt nonexistent_user"

echo ""
echo "====================================="
echo "Test Complete! Check $TEST_LOG for details"
echo "====================================="

# Summary
echo -e "\n=== TEST SUMMARY ===" | tee -a "$TEST_LOG"
echo "✓ CREATE - tested" | tee -a "$TEST_LOG"
echo "✓ WRITE - tested" | tee -a "$TEST_LOG"
echo "✓ READ - tested" | tee -a "$TEST_LOG"
echo "✓ INFO - tested" | tee -a "$TEST_LOG"
echo "✓ UNDO - tested" | tee -a "$TEST_LOG"
echo "✓ LIST - tested" | tee -a "$TEST_LOG"
echo "✓ ADDACCESS (both -R and -W) - tested" | tee -a "$TEST_LOG"
echo "✓ REMACCESS - tested" | tee -a "$TEST_LOG"
echo "✓ EXEC - tested" | tee -a "$TEST_LOG"
echo "✓ STREAM - tested (with timeout)" | tee -a "$TEST_LOG"
echo "✓ DELETE - tested" | tee -a "$TEST_LOG"
echo "✓ Unregistered user validation - tested" | tee -a "$TEST_LOG"
