#!/bin/bash
# Stop all running servers

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${RED}Stopping Docs++ servers...${NC}"

# Kill processes from PID files
if [ -f ".nm_pid" ]; then
    NM_PID=$(cat .nm_pid)
    if kill -0 $NM_PID 2>/dev/null; then
        kill $NM_PID
        echo -e "${GREEN}Stopped Name Server (PID: $NM_PID)${NC}"
    fi
    rm .nm_pid
fi

if [ -f ".ss_pid" ]; then
    SS_PID=$(cat .ss_pid)
    if kill -0 $SS_PID 2>/dev/null; then
        kill $SS_PID
        echo -e "${GREEN}Stopped Storage Server (PID: $SS_PID)${NC}"
    fi
    rm .ss_pid
fi

# Also try killing by name
pkill -f "name_server 8000" 2>/dev/null
pkill -f "storage_server SS1" 2>/dev/null

echo -e "${GREEN}All servers stopped${NC}"
