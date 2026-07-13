#!/bin/bash
# Build script - Clean and rebuild all components

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Get project root directory (parent of scripts/)
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}Building Docs++ Distributed File System${NC}"
echo -e "${YELLOW}========================================${NC}"
echo ""

# Function to build a component
build_component() {
    local dir=$1
    local name=$2
    
    echo -e "${YELLOW}Building $name...${NC}"
    cd "$PROJECT_DIR/$dir" || exit 1
    
    if make clean > /dev/null 2>&1; then
        echo -e "  Cleaned $name"
    fi
    
    if make; then
        echo -e "${GREEN}✓ $name built successfully${NC}"
        cd - > /dev/null
        return 0
    else
        echo -e "${RED}✗ Failed to build $name${NC}"
        cd - > /dev/null
        return 1
    fi
}

# Build all components
build_component "common" "Common Library" || exit 1
echo ""

build_component "name_server" "Name Server" || exit 1
echo ""

build_component "storage_server" "Storage Server" || exit 1
echo ""

build_component "client" "Client" || exit 1
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}All components built successfully!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next step:${NC}"
echo -e "  Run: ${GREEN}./scripts/start_all.sh${NC}"
echo ""
