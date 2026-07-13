#!/bin/bash
# Quick manual test verification

echo "=== Quick Command Status Check ==="
echo ""

CLIENT="/home/divlin/Desktop/Anshulz/iiit-coursework/sem-3/OSN/Assignments/course-project-one-project-after-another/client/client"

# Test each command one by one
echo "1. Testing CREATE..."
echo -e "testuser\nCREATE verify_test.txt\nEXIT" | $CLIENT 2>&1 | grep -i "created\|success\|error" | head -3

echo ""
echo "2. Testing INFO..."
echo -e "testuser\nINFO verify_test.txt\nEXIT" | $CLIENT 2>&1 | grep -E "(word_count|char_count|owner|Error)" | head -5

echo ""
echo "3. Testing LIST..."
echo -e "testuser\nLIST\nEXIT" | $CLIENT 2>&1 | grep -E "(testuser|-->)" | head -5

echo ""
echo "4. Testing ADDACCESS..."
echo -e "testuser\nADDACCESS -R verify_test.txt testuser2\nEXIT" | $CLIENT 2>&1 | grep -i "access\|granted\|error" | head -3

echo ""
echo "5. Testing DELETE..."
echo -e "testuser\nDELETE verify_test.txt\nEXIT" | $CLIENT 2>&1 | grep -i "delete\|success\|error" | head -3

echo ""
echo "=== Status Summary ==="
echo "Check above output for SUCCESS or ERROR messages"
