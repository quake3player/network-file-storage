#!/bin/bash

# Test VIEW command functionality
# This script creates test files and verifies VIEW works properly

# Get the project root directory
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CLIENT="$PROJECT_DIR/client/client"

echo "=== Testing VIEW Command ==="
echo ""

if [ ! -f "$CLIENT" ]; then
    echo "Error: Client executable not found at $CLIENT"
    exit 1
fi

# Test 1: Create files
echo "📝 Test 1: Creating test files..."
cat << 'EOF' | "$CLIENT"
DivyG
CREATE test1.txt
WRITE test1.txt 1
0 Hello
1 World
2 from
3 Test1
ETIRW
CREATE test2.txt
WRITE test2.txt 1
0 Another
1 test
2 file
ETIRW
CREATE my_script.sh
WRITE my_script.sh 1
0 echo
1 "Hello
2 from
3 script"
ETIRW
VIEW
EXIT
EOF

echo ""
echo "✓ Files created!"
echo ""

# Test 2: Test VIEW (user's files only)
echo "📋 Test 2: VIEW (user's accessible files)..."
echo "DivyG" | "$CLIENT" << 'EOF'
VIEW
EXIT
EOF

echo ""

# Test 3: Test VIEW -a (all files)
echo "📋 Test 3: VIEW -a (all files in system)..."
echo "DivyG" | "$CLIENT" << 'EOF'
VIEW -a
EXIT
EOF

echo ""

# Test 4: Test VIEW -l (with details)
echo "📋 Test 4: VIEW -l (with details)..."
echo "DivyG" | "$CLIENT" << 'EOF'
VIEW -l
EXIT
EOF

echo ""

# Test 5: Test VIEW -al (all files with details)
echo "📋 Test 5: VIEW -al (all files with details)..."
echo "DivyG" | "$CLIENT" << 'EOF'
VIEW -al
EXIT
EOF

echo ""

# Test 6: Test with second user (should have no access initially)
echo "📋 Test 6: Testing with second user (alice)..."
cat << 'EOF' | "$CLIENT"
alice
VIEW
VIEW -a
EXIT
EOF

echo ""

# Test 7: Grant access and verify
echo "📋 Test 7: Grant access to alice and verify..."
cat << 'EOF' | "$CLIENT"
DivyG
ADDACCESS -R test1.txt alice
EXIT
EOF

echo "Now alice should see test1.txt:"
cat << 'EOF' | "$CLIENT"
alice
VIEW
VIEW -a
EXIT
EOF

echo ""
echo "=== All VIEW Tests Complete! ==="
