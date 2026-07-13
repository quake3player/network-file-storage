#!/bin/bash

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Stopping all servers...${NC}"

# Kill Name Server
if pgrep -x "name_server" > /dev/null; then
    pkill -x "name_server"
    echo -e "Stopped Name Server"
else
    echo -e "${RED}Name Server not running${NC}"
fi

# Kill Storage Servers
if pgrep -x "storage_server" > /dev/null; then
    pkill -x "storage_server"
    echo -e "Stopped Storage Servers"
else
    echo -e "${RED}No Storage Servers running${NC}"
fi

# Kill Clients
# Use -f with ./client to match the running binary but avoid matching script arguments like --client
if pgrep -f "\./client" > /dev/null; then
    pkill -f "\./client"
    echo -e "Stopped Clients"
else
    echo -e "${RED}No Clients running${NC}"
fi

echo -e "${GREEN}All components stopped.${NC}"
