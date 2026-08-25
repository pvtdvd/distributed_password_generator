#!/bin/bash
#Author: Davide Pivato
#Configure UFW firewall rules for the distributed password generation cluster

set -e

CLUSTER_FILE="cluster.json"
SSH_KEY="$HOME/.ssh/id_ed25519"

#Check that cluster.json exists
if [[ ! -f "$CLUSTER_FILE" ]]; then
    echo "Error: $CLUSTER_FILE not found."
    exit 1
fi

#Extract master IP and port using Bash regular expressions
MASTER_IP=""
MASTER_PORT=""
IN_MASTER_SECTION=false

while IFS= read -r line; do
    if [[ $line =~ \"master\"[[:space:]]*:[[:space:]]*\{ ]]; then
        IN_MASTER_SECTION=true
        continue
    fi

    if [[ $IN_MASTER_SECTION == true && $line =~ \"ip\"[[:space:]]*:[[:space:]]*\"([^\"]+)\" ]]; then
        MASTER_IP="${BASH_REMATCH[1]}"
    fi

    if [[ $IN_MASTER_SECTION == true && $line =~ \"port\"[[:space:]]*:[[:space:]]*([0-9]+) ]]; then
        MASTER_PORT="${BASH_REMATCH[1]}"
        IN_MASTER_SECTION=false
    fi
done < "$CLUSTER_FILE"

if [[ -z "$MASTER_IP" || -z "$MASTER_PORT" ]]; then
    echo "Error: unable to read master IP or port from $CLUSTER_FILE."
    exit 1
fi

#Check that the SSH key exists
if [[ ! -f "$SSH_KEY" ]]; then
    echo "Error: SSH key $SSH_KEY not found."
    echo "Run the SSH key setup script first."
    exit 1
fi

#Check/install UFW on the master
if ! command -v ufw >/dev/null 2>&1; then
    echo "UFW is not installed on the master. Installing it..."
    sudo apt-get update
    sudo apt-get install -y ufw
fi

echo ""
echo "=== CONFIGURING MASTER FIREWALL ==="
echo "Master: $MASTER_IP:$MASTER_PORT"

#Reset existing UFW rules on the master
sudo ufw --force reset
sudo ufw default deny incoming
sudo ufw default allow outgoing

#Keep SSH access available on the master
sudo ufw allow 22/tcp comment 'SSH access'

#Read workers and allow ZeroMQ traffic only from their IP addresses
WORKER_REGEX='"name"[[:space:]]*:[[:space:]]*"([^"]+)"[[:space:]]*,[[:space:]]*"ip"[[:space:]]*:[[:space:]]*"([^"]+)"[[:space:]]*,[[:space:]]*"user"[[:space:]]*:[[:space:]]*"([^"]+)"'

while IFS= read -r line; do
    if [[ $line =~ $WORKER_REGEX ]]; then
        WORKER_NAME="${BASH_REMATCH[1]}"
        WORKER_IP="${BASH_REMATCH[2]}"

        echo "Allowing $WORKER_NAME ($WORKER_IP) to access TCP port $MASTER_PORT..."
        sudo ufw allow from "$WORKER_IP" to any port "$MASTER_PORT" proto tcp comment "ZeroMQ $WORKER_NAME"
    fi
done < "$CLUSTER_FILE"

sudo ufw --force enable

echo "Master firewall configured successfully."

#Configure every worker remotely through SSH
while IFS= read -r line; do
    if [[ $line =~ $WORKER_REGEX ]]; then
        WORKER_NAME="${BASH_REMATCH[1]}"
        WORKER_IP="${BASH_REMATCH[2]}"
        WORKER_USER="${BASH_REMATCH[3]}"

        echo ""
        echo "=== CONFIGURING $WORKER_NAME ($WORKER_IP) ==="

        ssh -i "$SSH_KEY" \
            -o StrictHostKeyChecking=no \
            -o UserKnownHostsFile=/dev/null \
            -o LogLevel=ERROR \
            "$WORKER_USER@$WORKER_IP" bash -s <<EOF_REMOTE
set -e

if ! command -v ufw >/dev/null 2>&1; then
    echo "Installing UFW..."
    apt-get update
    apt-get install -y ufw
fi

ufw --force reset
ufw default deny incoming
ufw default allow outgoing

# Workers only need incoming SSH access from the master
ufw allow from "$MASTER_IP" to any port 22 proto tcp comment 'SSH from master'

ufw --force enable
ufw status verbose
EOF_REMOTE

        echo "$WORKER_NAME firewall configured successfully."
    fi
done < "$CLUSTER_FILE"

echo ""
echo "=== FIREWALL CONFIGURATION COMPLETED ==="
echo "Master: TCP port $MASTER_PORT accepts connections only from configured workers."
echo "Workers: incoming traffic is denied except SSH from $MASTER_IP."
