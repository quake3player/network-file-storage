#!/bin/bash
# Quick Start Script for Docs++ Distributed File System
# This script starts the Name Server and Storage Server for testing

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting Docs++ Distributed File System${NC}"
echo "=========================================="

# Create data directories
echo -e "${YELLOW}Creating data directories...${NC}"
mkdir -p nm_data/acl nm_data/cache nm_data/logs
mkdir -p ss_data/files ss_data/metadata ss_data/undo ss_data/logs

# Check if executables exist
if [ ! -f "name_server/name_server" ]; then
    echo -e "${RED}Error: name_server executable not found. Run 'make' in name_server/${NC}"
    exit 1
fi

if [ ! -f "storage_server/storage_server" ]; then
    echo -e "${RED}Error: storage_server executable not found. Run 'make' in storage_server/${NC}"
    exit 1
fi

if [ ! -f "client/client" ]; then
    echo -e "${RED}Error: client executable not found. Run 'make' in client/${NC}"
    exit 1
fi

# Start Name Server in background
echo -e "${YELLOW}Starting Name Server on port 8000...${NC}"
cd name_server
./name_server 8000 ../nm_data > ../nm_data/logs/nm.log 2>&1 &
NM_PID=$!
cd ..
echo -e "${GREEN}Name Server started (PID: $NM_PID)${NC}"

# Wait a bit for NM to initialize
sleep 1

# Start Storage Server in background
echo -e "${YELLOW}Starting Storage Server SS1 on port 9002 (Client)...${NC}"
cd storage_server
./storage_server localhost 8000 SS1 9002 > ../ss_data/logs/ss1.log 2>&1 &
SS_PID=$!
cd ..
echo -e "${GREEN}Storage Server started (PID: $SS_PID)${NC}"

# Wait for SS to register
sleep 1

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}System is ready!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Name Server:    localhost:8000 (PID: $NM_PID)"
echo "Storage Server: localhost:9002 (Client) (PID: $SS_PID)"
echo ""
echo "Logs:"
echo "  - Name Server:    nm_data/logs/nm.log"
echo "  - Storage Server: ss_data/logs/ss1.log"
echo ""
echo -e "${YELLOW}To start a client:${NC}"
echo "  cd client && ./client"
echo ""
echo -e "${YELLOW}To stop the servers:${NC}"
echo "  kill $NM_PID $SS_PID"
echo "  or run: ./stop_servers.sh"
echo ""
echo -e "${YELLOW}To view logs:${NC}"
echo "  tail -f nm_data/logs/nm.log"
echo "  tail -f ss_data/logs/ss1.log"
echo ""

# Save PIDs to file for easy cleanup
echo "$NM_PID" > .nm_pid
echo "$SS_PID" > .ss_pid

echo -e "${GREEN}Press Enter to start a client, or Ctrl+C to exit${NC}"
read -r

# Start client
cd client
./client
