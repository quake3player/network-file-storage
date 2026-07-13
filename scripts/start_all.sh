#!/bin/bash

# ==============================================================================
# Docs++ Multi-Component Launcher
# Starts Name Server, Storage Servers, and Clients in separate terminal windows.
# ==============================================================================

# --- Colors ---
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# --- Configuration ---
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$(basename "$0")"

# Defaults
NM_HOST="localhost"
NM_PORT=8000
declare -a STORAGE_SPECS=()
declare -a CLIENT_USERS=()
STORAGE_COUNT_EXPECTED=-1
CLIENT_COUNT_EXPECTED=-1

# --- Helper Functions ---

print_usage() {
    cat <<EOF
${GREEN}Docs++ Multi-Component Launcher${NC}

Usage: ./scripts/${SCRIPT_NAME} [OPTIONS]

Examples:
  # Default: 1 storage server, 1 client (prompts for username)
  ./scripts/${SCRIPT_NAME}

  # Custom topology: 2 storage servers, 2 clients
  ./scripts/${SCRIPT_NAME} \\
      --nm-port 8000 \\
      --storage-count 2 \\
      --storage SS1:9002 --storage SS2:9003 \\
      --client-count 2 \\
      --client alice --client bob

Options:
  --nm-host HOST          Name Server host (default: localhost)
  --nm-port PORT          Name Server port (default: 8000)
  --storage-count N       Expected number of storage servers
  --storage ID:PORT       Storage server spec (ID and port)
  --client-count M        Expected number of clients
  --client USERNAME       Client username
  -h, --help              Show this help

EOF
}

is_int() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

ensure_value() {
    if [ -z "$2" ] || [[ "$2" == --* ]]; then
        echo -e "${RED}Error: Option $1 requires a value.${NC}" >&2
        exit 1
    fi
}

# --- Argument Parsing ---

while [[ $# -gt 0 ]]; do
    case "$1" in
        --nm-host)
            ensure_value "$1" "$2"
            NM_HOST="$2"
            shift 2
            ;;
        --nm-port)
            ensure_value "$1" "$2"
            if ! is_int "$2" || [ "$2" -lt 1 ] || [ "$2" -gt 65535 ]; then
                echo -e "${RED}Error: Invalid port '$2'${NC}" >&2; exit 1
            fi
            NM_PORT="$2"
            shift 2
            ;;
        --storage-count)
            ensure_value "$1" "$2"
            STORAGE_COUNT_EXPECTED="$2"
            shift 2
            ;;
        --storage)
            ensure_value "$1" "$2"
            STORAGE_SPECS+=("$2")
            shift 2
            ;;
        --client-count)
            ensure_value "$1" "$2"
            CLIENT_COUNT_EXPECTED="$2"
            shift 2
            ;;
        --client)
            ensure_value "$1" "$2"
            CLIENT_USERS+=("$2")
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}" >&2
            print_usage
            exit 1
            ;;
    esac
done

# --- Apply Defaults ---

# If no storage provided, default to SS1 on port 9002
if [ ${#STORAGE_SPECS[@]} -eq 0 ]; then
    STORAGE_SPECS=("SS1:9002")
fi

# If no client provided, default to user1
if [ ${#CLIENT_USERS[@]} -eq 0 ]; then
    CLIENT_USERS=("user1")
fi

# --- Validation ---

# Validate Storage Specs
if [ "$STORAGE_COUNT_EXPECTED" -ne -1 ] && [ ${#STORAGE_SPECS[@]} -ne "$STORAGE_COUNT_EXPECTED" ]; then
    echo -e "${RED}Error: Expected $STORAGE_COUNT_EXPECTED storage servers, but defined ${#STORAGE_SPECS[@]}.${NC}" >&2
    exit 1
fi

declare -a STORAGE_IDS
declare -a STORAGE_PORTS

for spec in "${STORAGE_SPECS[@]}"; do
    if [[ ! "$spec" =~ ^[^:]+:[0-9]+$ ]]; then
        echo -e "${RED}Error: Invalid storage spec '$spec'. Format must be ID:PORT${NC}" >&2
        exit 1
    fi
    IFS=':' read -r sid sport <<< "$spec"
    STORAGE_IDS+=("$sid")
    STORAGE_PORTS+=("$sport")
done

# Validate Client Specs
if [ "$CLIENT_COUNT_EXPECTED" -ne -1 ] && [ ${#CLIENT_USERS[@]} -ne "$CLIENT_COUNT_EXPECTED" ]; then
    echo -e "${RED}Error: Expected $CLIENT_COUNT_EXPECTED clients, but defined ${#CLIENT_USERS[@]}.${NC}" >&2
    exit 1
fi

# Check Executables
if [ ! -f "$PROJECT_DIR/name_server/name_server" ]; then
    echo -e "${RED}Error: name_server binary not found at $PROJECT_DIR/name_server/name_server${NC}"
    echo "Please build the project first."
    exit 1
fi
if [ ! -f "$PROJECT_DIR/storage_server/storage_server" ]; then
    echo -e "${RED}Error: storage_server binary not found.${NC}"; exit 1
fi
if [ ! -f "$PROJECT_DIR/client/client" ]; then
    echo -e "${RED}Error: client binary not found.${NC}"; exit 1
fi

# --- Terminal Detection ---

detect_terminal() {
    if command -v kitty >/dev/null 2>&1; then echo "kitty"
    elif command -v gnome-terminal >/dev/null 2>&1; then echo "gnome-terminal"
    elif command -v konsole >/dev/null 2>&1; then echo "konsole"
    elif command -v xterm >/dev/null 2>&1; then echo "xterm"
    elif command -v tmux >/dev/null 2>&1; then echo "tmux"
    else echo "none"; fi
}

TERMINAL=$(detect_terminal)

if [ "$TERMINAL" = "none" ]; then
    echo -e "${RED}Error: No supported terminal found (kitty, gnome-terminal, konsole, xterm, tmux).${NC}"
    exit 1
fi

echo -e "${GREEN}Starting Docs++ using $TERMINAL${NC}"
echo -e "${YELLOW}Name Server:${NC} $NM_HOST:$NM_PORT"
echo -e "${YELLOW}Storage Servers:${NC} ${#STORAGE_IDS[@]}"
echo -e "${YELLOW}Clients:${NC} ${#CLIENT_USERS[@]}"
echo ""

# --- Launch Logic ---

TMUX_SESSION="docspp_$$"
TMUX_CREATED=0

launch_tmux() {
    local title="$1" dir="$2" cmd="$3"
    local full_cmd="cd \"$dir\" && $cmd; bash"
    
    if [ $TMUX_CREATED -eq 0 ]; then
        tmux new-session -d -s "$TMUX_SESSION" -n "$title" "$full_cmd"
        TMUX_CREATED=1
    else
        tmux new-window -t "$TMUX_SESSION" -n "$title" "$full_cmd"
    fi
}

launch_gui() {
    local title="$1" dir="$2" cmd="$3" hold_msg="$4"
    local full_cmd="$cmd; echo ''; echo '$hold_msg'; read"

    case "$TERMINAL" in
        kitty)
            kitty --title "$title" --directory "$dir" bash -c "$full_cmd" & ;;
        gnome-terminal)
            gnome-terminal --title="$title" --working-directory="$dir" -- bash -c "$full_cmd" ;;
        konsole)
            konsole --new-tab --workdir "$dir" -e bash -c "$full_cmd" & ;;
        xterm)
            xterm -title "$title" -e "cd \"$dir\" && $full_cmd" & ;;
    esac
    sleep 0.5
}

launch_process() {
    if [ "$TERMINAL" = "tmux" ]; then
        launch_tmux "$1" "$2" "$3"
    else
        launch_gui "$1" "$2" "$3" "$4"
    fi
}

# --- Execution ---

# 1. Start Name Server
launch_process "Name Server" \
    "$PROJECT_DIR/name_server" \
    "./name_server $NM_PORT ../nm_data" \
    "Name Server exited. Press Enter to close."

# 2. Start Storage Servers
for i in "${!STORAGE_IDS[@]}"; do
    launch_process "Storage ${STORAGE_IDS[$i]}" \
        "$PROJECT_DIR/storage_server" \
        "./storage_server $NM_HOST $NM_PORT ${STORAGE_IDS[$i]} ${STORAGE_PORTS[$i]}" \
        "Storage Server exited. Press Enter to close."
done

# 3. Start Clients
for username in "${CLIENT_USERS[@]}"; do
    # Logic: Pipe the username into the client so it auto-logins
    CLIENT_CMD="{ echo '$username'; cat; } | ./client $NM_HOST $NM_PORT"
    
    launch_process "Client ($username)" \
        "$PROJECT_DIR/client" \
        "$CLIENT_CMD" \
        "Client exited. Press Enter to close."
done

# --- Post-Launch ---

if [ "$TERMINAL" = "tmux" ]; then
    echo -e "${GREEN}Attaching to tmux session...${NC}"
    tmux attach-session -t "$TMUX_SESSION"
else
    echo -e "${GREEN}All components launched!${NC}"
    echo -e "${YELLOW}To stop all components, run:${NC} ./stop_servers.sh"
    echo "or press Ctrl+C in the individual windows."
fi