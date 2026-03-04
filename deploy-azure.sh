#!/bin/bash
# =============================================================
# chat_cli Azure Deployment Script
# Provisions an Azure VM and deploys the chat server
# =============================================================

set -e

# ---- Configuration (edit these if you want) ----
RESOURCE_GROUP="chat-cli-rg"
VM_NAME="chat-server"
LOCATION="eastus"
VM_SIZE="Standard_B1s"
VM_IMAGE="Ubuntu2204"
ADMIN_USER="azureuser"
CHAT_PORT=8443
HEALTH_PORT=8080
REPO_URL="https://github.com/AasishDairelSahayaGrinspan/chat_cli.git"
# -------------------------------------------------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log()   { echo -e "${GREEN}[+]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[x]${NC} $1"; exit 1; }
info()  { echo -e "${CYAN}[*]${NC} $1"; }

# ---- Pre-flight checks ----
echo ""
echo "============================================="
echo "   chat_cli - Azure Deployment Script"
echo "============================================="
echo ""

if ! command -v az &> /dev/null; then
    error "Azure CLI not found. Install it:\n    curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash"
fi

# Check login
if ! az account show &> /dev/null 2>&1; then
    warn "Not logged into Azure. Opening login..."
    az login
fi

ACCOUNT=$(az account show --query name -o tsv)
SUBSCRIPTION=$(az account show --query id -o tsv)
info "Azure account: $ACCOUNT"
info "Subscription:  $SUBSCRIPTION"
echo ""
read -p "Continue with this account? (y/n) " -n 1 -r
echo ""
[[ ! $REPLY =~ ^[Yy]$ ]] && error "Aborted."

# ---- Step 1: Resource Group ----
log "Creating resource group '$RESOURCE_GROUP' in '$LOCATION'..."
az group create \
    --name "$RESOURCE_GROUP" \
    --location "$LOCATION" \
    --output none

# ---- Step 2: Create VM ----
log "Creating VM '$VM_NAME' (size: $VM_SIZE)... This takes 1-2 minutes."
VM_OUTPUT=$(az vm create \
    --resource-group "$RESOURCE_GROUP" \
    --name "$VM_NAME" \
    --image "$VM_IMAGE" \
    --size "$VM_SIZE" \
    --admin-username "$ADMIN_USER" \
    --generate-ssh-keys \
    --public-ip-sku Standard \
    --output json)

PUBLIC_IP=$(echo "$VM_OUTPUT" | grep -o '"publicIpAddress": "[^"]*"' | cut -d'"' -f4)

if [ -z "$PUBLIC_IP" ]; then
    error "Failed to get public IP. Check Azure portal."
fi

log "VM created! Public IP: $PUBLIC_IP"

# ---- Step 3: Open Ports ----
log "Opening port $CHAT_PORT (chat)..."
az vm open-port \
    --resource-group "$RESOURCE_GROUP" \
    --name "$VM_NAME" \
    --port "$CHAT_PORT" \
    --priority 1000 \
    --output none

log "Opening port $HEALTH_PORT (health)..."
az vm open-port \
    --resource-group "$RESOURCE_GROUP" \
    --name "$VM_NAME" \
    --port "$HEALTH_PORT" \
    --priority 1001 \
    --output none

# ---- Step 4: Wait for SSH ----
log "Waiting for SSH to be ready..."
for i in {1..30}; do
    if ssh -o StrictHostKeyChecking=no -o ConnectTimeout=5 "$ADMIN_USER@$PUBLIC_IP" "echo ok" &> /dev/null; then
        break
    fi
    sleep 2
done

# ---- Step 5: Setup server remotely ----
log "Setting up chat server on the VM..."

ssh -o StrictHostKeyChecking=no "$ADMIN_USER@$PUBLIC_IP" bash <<'REMOTE_SCRIPT'
set -e

echo "[+] Updating system..."
sudo apt-get update -qq
sudo apt-get upgrade -y -qq

echo "[+] Installing Docker..."
sudo apt-get install -y -qq docker.io docker-compose git curl
sudo systemctl enable docker
sudo systemctl start docker
sudo usermod -aG docker $USER

echo "[+] Cloning chat_cli..."
sudo rm -rf /opt/chat_cli
sudo git clone https://github.com/AasishDairelSahayaGrinspan/chat_cli.git /opt/chat_cli
sudo chown -R $USER:$USER /opt/chat_cli

echo "[+] Generating TLS certificates..."
cd /opt/chat_cli/docker/certs
chmod +x generate.sh
./generate.sh

echo "[+] Starting chat server with Docker..."
cd /opt/chat_cli/docker
sudo docker-compose up -d --build

echo "[+] Waiting for server to start..."
sleep 10

echo "[+] Checking health..."
if curl -sf http://localhost:8080/healthz > /dev/null 2>&1; then
    echo "[+] Server is healthy!"
else
    echo "[!] Health check didn't pass yet. It may still be starting."
    echo "    Check with: sudo docker-compose logs -f"
fi
REMOTE_SCRIPT

# ---- Done! ----
echo ""
echo "============================================="
echo -e "${GREEN}   Deployment Complete!${NC}"
echo "============================================="
echo ""
echo "  Server IP:     $PUBLIC_IP"
echo "  Chat port:     $CHAT_PORT"
echo "  Health check:  http://$PUBLIC_IP:$HEALTH_PORT/healthz"
echo ""
echo "  Connect with:"
echo "    ./build/client/chat_client $PUBLIC_IP $CHAT_PORT"
echo ""
echo "  SSH into server:"
echo "    ssh $ADMIN_USER@$PUBLIC_IP"
echo ""
echo "  View logs:"
echo "    ssh $ADMIN_USER@$PUBLIC_IP 'cd /opt/chat_cli/docker && sudo docker-compose logs -f'"
echo ""
echo "  Tear down everything:"
echo "    az group delete --name $RESOURCE_GROUP --yes --no-wait"
echo ""
echo "============================================="
