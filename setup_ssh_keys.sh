#!/bin/bash
#Author: Davide Pivato
#Generate and distribute Ed25519 SSH keys for the cluster

set -e  #Exit on any error

CLUSTER_FILE="cluster.json"
SSH_KEY="$HOME/.ssh/id_ed25519"
SSH_KEY_PUB="$SSH_KEY.pub"

echo "=== SSH KEY SETUP FOR DISTRIBUTED PASSWORD GENERATOR ==="

#Check that cluster.json exists
if [[ ! -f "$CLUSTER_FILE" ]]; then
    echo "Error: $CLUSTER_FILE not found in current directory."
    exit 1
fi

#Generate Ed25519 key pair if it doesn't already exist
if [[ ! -f "$SSH_KEY" ]]; then
    echo "SSH key not found. Generating new Ed25519 key pair..."
    ssh-keygen -t ed25519 -f "$SSH_KEY" -N "" -C "master@distributed-password-generator"
    echo "Key pair generated: $SSH_KEY"
else
    echo "SSH key already exists: $SSH_KEY"
fi

#Extract the list of workers from cluster.json
#Use jq if available, otherwise fallback to a simpler regex parser
if command -v jq &>/dev/null; then
    echo "Using jq to parse JSON."
    WORKER_LIST=$(jq -r '.workers[] | "\(.ip) \(.user)"' "$CLUSTER_FILE")
else
    echo "jq not found, using regex fallback."
    #Assumes each worker definition is on a single line (same format as firewall.sh)
    WORKER_LIST=$(grep -E '"name"' "$CLUSTER_FILE" | while read -r line; do
        ip=$(echo "$line" | grep -oP '"ip"[[:space:]]*:[[:space:]]*"\K[^"]+')
        user=$(echo "$line" | grep -oP '"user"[[:space:]]*:[[:space:]]*"\K[^"]+')
        #If no user field is found, default to "root"
        if [[ -z "$user" ]]; then
            user="root"
        fi
        echo "$ip $user"
    done)
fi

if [[ -z "$WORKER_LIST" ]]; then
    echo "Error: no workers found in $CLUSTER_FILE."
    exit 1
fi

#Copy the public key to each worker using ssh-copy-id
echo "Starting distribution of public key to workers..."
while IFS= read -r entry; do
    worker_ip=$(echo "$entry" | awk '{print $1}')
    worker_user=$(echo "$entry" | awk '{print $2}')
    
    #Fallback default user if extraction missed it
    if [[ -z "$worker_user" ]]; then
        worker_user="root"
    fi

    echo "Configuring $worker_user@$worker_ip..."
    
    #Copy the public key using ssh-copy-id with non-interactive options
    ssh-copy-id -i "$SSH_KEY" \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR \
        "$worker_user@$worker_ip" \
        < /dev/null

    if [[ $? -eq 0 ]]; then
        echo "Success: key installed on $worker_user@$worker_ip"
    else
        echo "Failed: could not install key on $worker_user@$worker_ip"
        echo "Please check SSH connectivity and password."
        exit 1
    fi
done <<< "$WORKER_LIST"

echo "=== SSH KEY SETUP COMPLETED ==="
echo "The master can now connect to all workers without password."
