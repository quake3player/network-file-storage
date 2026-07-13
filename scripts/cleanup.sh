#!/bin/bash
# Cleanup script - Remove all runtime data and build artifacts

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Cleanup: Docs++ Runtime & Build Files${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""

# Stop any running servers first
echo -e "${YELLOW}Stopping any running servers...${NC}"
if pkill -f name_server > /dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} Stopped name_server"
else
    echo -e "  ${YELLOW}ℹ${NC} No name_server running"
fi

if pkill -f storage_server > /dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} Stopped storage_server"
else
    echo -e "  ${YELLOW}ℹ${NC} No storage_server running"
fi

sleep 1
echo ""

# Remove PID files
echo -e "${YELLOW}Removing PID files...${NC}"
if [ -f "$PROJECT_DIR/.nm_pid" ]; then
    rm -f "$PROJECT_DIR/.nm_pid"
    echo -e "  ${GREEN}✓${NC} Removed .nm_pid"
fi

if [ -f "$PROJECT_DIR/.ss_pid" ]; then
    rm -f "$PROJECT_DIR/.ss_pid"
    echo -e "  ${GREEN}✓${NC} Removed .ss_pid"
fi
echo ""

# Remove nm_data (Name Server data)
echo -e "${YELLOW}Removing Name Server data...${NC}"
if [ -d "$PROJECT_DIR/nm_data" ]; then
    rm -rf "$PROJECT_DIR/nm_data"
    echo -e "  ${GREEN}✓${NC} Removed nm_data/"
else
    echo -e "  ${YELLOW}ℹ${NC} nm_data/ not found"
fi
echo ""

# Remove storage server data directories
echo -e "${YELLOW}Removing Storage Server data...${NC}"
if [ -d "$PROJECT_DIR/storage_server/ss_data" ]; then
    rm -rf "$PROJECT_DIR/storage_server/ss_data"
    echo -e "  ${GREEN}✓${NC} Removed storage_server/ss_data/"
else
    echo -e "  ${YELLOW}ℹ${NC} storage_server/ss_data/ not found"
fi

# Also check for ss_data in project root (if it exists there)
if [ -d "$PROJECT_DIR/ss_data" ]; then
    rm -rf "$PROJECT_DIR/ss_data"
    echo -e "  ${GREEN}✓${NC} Removed ss_data/"
fi
echo ""

# Remove build artifacts
echo -e "${YELLOW}Cleaning build artifacts...${NC}"

# Function to clean a component
clean_component() {
    local dir=$1
    local name=$2
    
    if [ -d "$PROJECT_DIR/$dir" ]; then
        cd "$PROJECT_DIR/$dir" || return
        if make clean > /dev/null 2>&1; then
            echo -e "  ${GREEN}✓${NC} Cleaned $name"
        fi
        cd - > /dev/null || return
    fi
}

clean_component "common" "common/"
clean_component "name_server" "name_server/"
clean_component "storage_server" "storage_server/"
clean_component "client" "client/"

# Remove executables explicitly (in case make clean doesn't)
if [ -f "$PROJECT_DIR/name_server/name_server" ]; then
    rm -f "$PROJECT_DIR/name_server/name_server"
    echo -e "  ${GREEN}✓${NC} Removed name_server executable"
fi

if [ -f "$PROJECT_DIR/storage_server/storage_server" ]; then
    rm -f "$PROJECT_DIR/storage_server/storage_server"
    echo -e "  ${GREEN}✓${NC} Removed storage_server executable"
fi

if [ -f "$PROJECT_DIR/client/client" ]; then
    rm -f "$PROJECT_DIR/client/client"
    echo -e "  ${GREEN}✓${NC} Removed client executable"
fi
echo ""

# Remove test result files
echo -e "${YELLOW}Removing test results...${NC}"
if [ -f "$PROJECT_DIR/TEST_RESULTS.txt" ]; then
    rm -f "$PROJECT_DIR/TEST_RESULTS.txt"
    echo -e "  ${GREEN}✓${NC} Removed TEST_RESULTS.txt"
fi

# Remove any log files in root
if ls "$PROJECT_DIR"/*.log > /dev/null 2>&1; then
    rm -f "$PROJECT_DIR"/*.log
    echo -e "  ${GREEN}✓${NC} Removed *.log files"
fi
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Cleanup complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Removed:${NC}"
echo "  • PID files (.nm_pid, .ss_pid)"
echo "  • Runtime data (nm_data/, ss_data/)"
echo "  • Build artifacts (*.o, *.a, executables)"
echo "  • Test results and logs"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Build: ${GREEN}./scripts/build_all.sh${NC}"
echo "  2. Start: ${GREEN}./scripts/start_all.sh${NC}"
echo ""
