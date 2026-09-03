#!/usr/bin/env bash
# =============================================================================
# ec2-setup.sh — Run ONCE on EC2 to install dependencies and set up systemd.
#
# Usage (on EC2):
#   chmod +x ec2-setup.sh && sudo ./ec2-setup.sh
# =============================================================================
set -euo pipefail

REPO_DIR="/home/${SUDO_USER:-ubuntu}/Load-Balancer-CPP"
SERVICE_NAME="lb"
SERVICE_USER="${SUDO_USER:-ubuntu}"

echo "======================================================================"
echo " C++ Load Balancer — EC2 One-Time Setup"
echo "======================================================================"

# ── Detect distro ────────────────────────────────────────────────────────────
if command -v apt-get &>/dev/null; then
    DISTRO="debian"
elif command -v yum &>/dev/null || command -v dnf &>/dev/null; then
    DISTRO="rhel"
else
    echo "ERROR: Unsupported distro. Install g++, make, cmake manually."; exit 1
fi

# ── Install build tools ──────────────────────────────────────────────────────
echo ""
echo "==> Installing build tools..."
if [[ "$DISTRO" == "debian" ]]; then
    apt-get update -qq
    apt-get install -y -qq build-essential cmake git curl
else
    # Amazon Linux 2023 / RHEL
    dnf install -y gcc-c++ make cmake git curl 2>/dev/null || \
    yum install -y gcc-c++ make cmake git curl
fi
echo "    Build tools ready."

# ── Build the project ────────────────────────────────────────────────────────
if [[ -d "$REPO_DIR" ]]; then
    echo ""
    echo "==> Building load_balancer + echo_server in $REPO_DIR..."
    cd "$REPO_DIR"
    (cd load-balancer && make -s load_balancer)
    (cd server        && make -s echo_server)
    echo "    Build complete."
else
    echo ""
    echo "WARNING: $REPO_DIR not found. Run ec2-deploy.sh from your local machine first,"
    echo "         then re-run this script."
fi

# ── Install systemd service ──────────────────────────────────────────────────
echo ""
echo "==> Installing systemd service '$SERVICE_NAME'..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Make wrapper scripts executable
chmod +x "$SCRIPT_DIR/start-all.sh" "$SCRIPT_DIR/ec2-deploy.sh" 2>/dev/null || true

# Copy the hardened service file
if [[ -f "$SCRIPT_DIR/lb.service" ]]; then
    cp "$SCRIPT_DIR/lb.service" /etc/systemd/system/${SERVICE_NAME}.service
    # Patch username in service file to match actual user
    sed -i "s/^User=ubuntu/User=${SERVICE_USER}/" /etc/systemd/system/${SERVICE_NAME}.service
    sed -i "s/^Group=ubuntu/Group=${SERVICE_USER}/" /etc/systemd/system/${SERVICE_NAME}.service
    sed -i "s|/home/ubuntu|/home/${SERVICE_USER}|g" /etc/systemd/system/${SERVICE_NAME}.service
fi

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}
echo "    systemd service installed and enabled."


# ── Open ports reminder ──────────────────────────────────────────────────────
echo ""
echo "======================================================================"
echo " IMPORTANT: Open these ports in your EC2 Security Group:"
echo "   TCP 8080  — Load Balancer (client traffic)"
echo "   TCP 8081  — Stats endpoint (dashboard polling)"
echo "   TCP 22    — SSH (already open)"
echo "======================================================================"
echo ""
echo " To start:   sudo systemctl start ${SERVICE_NAME}"
echo " To stop:    sudo systemctl stop  ${SERVICE_NAME}"
echo " To status:  sudo systemctl status ${SERVICE_NAME}"
echo " Logs:       sudo journalctl -u ${SERVICE_NAME} -f"
echo ""
echo " Stats API:  curl http://localhost:8081/stats"
echo "======================================================================"
