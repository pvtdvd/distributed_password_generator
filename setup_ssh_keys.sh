#!/bin/bash
# setup_ssh_keys.sh - Generate and distribute Ed25519 SSH keys for the cluster

set -e  # Exit on any error

CLUSTER_FILE="cluster.json"
SSH_KEY="$HOME/.ssh/id_ed25519"
SSH_KEY_PUB="$SSH_KEY.pub"

# Colours for terminal output (optional but nice)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== SSH KEY SETUP FOR DISTRIBUTED PASSWORD GENERATOR ===${NC}"

# 1. Check that cluster.json exists
if [[ ! -f "$CLUSTER_FILE" ]]; then
    echo -e "${RED}Error: $CLUSTER_FILE not found in current directory.${NC}"
    exit 1
fi

# 2. Generate Ed25519 key pair if it doesn't already exist
if [[ ! -f "$SSH_KEY" ]]; then
    echo -e "${YELLOW}SSH key not found. Generating new Ed25519 key pair...${NC}"
    ssh-keygen -t ed25519 -f "$SSH_KEY" -N "" -C "master@distributed-password-generator"
    echo -e "${GREEN}Key pair generated: $SSH_KEY${NC}"
else
    echo -e "${GREEN}SSH key already exists: $SSH_KEY${NC}"
fi

# 3. Extract the list of workers from cluster.json
# Use jq if available, otherwise fallback to a simpler regex parser
if command -v jq &>/dev/null; then
    echo -e "${GREEN}Using jq to parse JSON.${NC}"
    WORKER_LIST=$(jq -r '.workers[] | "\(.ip) \(.user)"' "$CLUSTER_FILE")
else
    echo -e "${YELLOW}jq not found, using regex fallback.${NC}"
    # Assumes each worker definition is on a single line (same format as firewall.sh)
    WORKER_LIST=$(grep -E '"name"' "$CLUSTER_FILE" | while read -r line; do
        ip=$(echo "$line" | grep -oP '"ip"[[:space:]]*:[[:space:]]*"\K[^"]+')
        user=$(echo "$line" | grep -oP '"user"[[:space:]]*:[[:space:]]*"\K[^"]+')
        # If no user field is found, default to "root"
        if [[ -z "$user" ]]; then
            user="root"
        fi
        echo "$ip $user"
    done)
fi

if [[ -z "$WORKER_LIST" ]]; then
    echo -e "${RED}Error: no workers found in $CLUSTER_FILE.${NC}"
    exit 1
fi

# 4. Copy the public key to each worker using ssh-copy-id
echo -e "${GREEN}Starting distribution of public key to workers...${NC}"
while IFS= read -r entry; do
    worker_ip=$(echo "$entry" | awk '{print $1}')
    worker_user=$(echo "$entry" | awk '{print $2}')
    
    # Fallback default user if extraction missed it
    if [[ -z "$worker_user" ]]; then
        worker_user="root"
    fi

    echo -e "${YELLOW}Configuring $worker_user@$worker_ip...${NC}"
    
    # Copy the public key using ssh-copy-id with non-interactive options
    ssh-copy-id -i "$SSH_KEY" \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR \
        "$worker_user@$worker_ip" \
        < /dev/null

    if [[ $? -eq 0 ]]; then
        echo -e "${GREEN}Success: key installed on $worker_user@$worker_ip${NC}"
    else
        echo -e "${RED}Failed: could not install key on $worker_user@$worker_ip${NC}"
        echo -e "${RED}Please check SSH connectivity and password.${NC}"
        exit 1
    fi
done <<< "$WORKER_LIST"

echo -e "${GREEN}=== SSH KEY SETUP COMPLETED ===${NC}"
echo -e "The master can now connect to all workers without password."
echo -e "You can test with: ssh -i $SSH_KEY <user>@<worker_ip>"