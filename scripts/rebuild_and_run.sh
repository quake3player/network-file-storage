#!/bin/bash

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPTS_DIR="$PROJECT_DIR/scripts"

CLEAN=0
ARGS=()

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN=1
            shift
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

echo -e "${YELLOW}========================================${NC}"
echo -e "${YELLOW}   Rebuild and Run Sequence${NC}"
echo -e "${YELLOW}========================================${NC}"

# 1. Stop
if [ -f "$SCRIPTS_DIR/stop_servers.sh" ]; then
    echo -e "\n${YELLOW}[1/4] Stopping servers...${NC}"
    "$SCRIPTS_DIR/stop_servers.sh"
else
    echo -e "${RED}Error: stop_servers.sh not found${NC}"
    exit 1
fi

# 2. Clean (Optional)
if [ $CLEAN -eq 1 ]; then
    echo -e "\n${YELLOW}[2/4] Cleaning...${NC}"
    if [ -f "$SCRIPTS_DIR/cleanup.sh" ]; then
        "$SCRIPTS_DIR/cleanup.sh"
    else
        echo -e "${RED}Error: cleanup.sh not found${NC}"
        exit 1
    fi
else
    echo -e "\n${YELLOW}[2/4] Skipping cleanup (use --clean to enable)${NC}"
fi

# 3. Build
echo -e "\n${YELLOW}[3/4] Building...${NC}"
if [ -f "$SCRIPTS_DIR/build_all.sh" ]; then
    "$SCRIPTS_DIR/build_all.sh"
    if [ $? -ne 0 ]; then
        echo -e "${RED}Build failed! Aborting start.${NC}"
        exit 1
    fi
else
    echo -e "${RED}Error: build_all.sh not found${NC}"
    exit 1
fi

# 4. Start
echo -e "\n${YELLOW}[4/4] Starting...${NC}"
if [ -f "$SCRIPTS_DIR/start_all.sh" ]; then
    "$SCRIPTS_DIR/start_all.sh" "${ARGS[@]}"
else
    echo -e "${RED}Error: start_all.sh not found${NC}"
    exit 1
fi
