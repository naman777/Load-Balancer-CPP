#!/usr/bin/env bash
# =============================================================================
# ec2-deploy.sh — Run LOCALLY to push code to EC2, build, and restart the LB.
#
# Usage:
#   ./scripts/ec2-deploy.sh <EC2_IP> <SSH_KEY_PATH> [EC2_USER]
#
# Examples:
#   ./scripts/ec2-deploy.sh 3.142.55.100 ~/.ssh/my-key.pem
#   ./scripts/ec2-deploy.sh 3.142.55.100 ~/.ssh/my-key.pem ec2-user
#
# After first run, also run on EC2:
#   sudo ~/Load-Balancer-CPP/scripts/ec2-setup.sh
# =============================================================================
set -euo pipefail

# ── Args ─────────────────────────────────────────────────────────────────────
EC2_IP="${1:-}"
SSH_KEY="${2:-}"
EC2_USER="${3:-ubuntu}"

if [[ -z "$EC2_IP" || -z "$SSH_KEY" ]]; then
    echo "Usage: $0 <EC2_IP> <SSH_KEY_PATH> [EC2_USER=ubuntu]"
    echo ""
    echo "  EC2_IP      — public IP or hostname of your EC2 instance"
    echo "  SSH_KEY     — path to your .pem file"
    echo "  EC2_USER    — ubuntu (Ubuntu) | ec2-user (Amazon Linux)"
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REMOTE="$EC2_USER@$EC2_IP"
REMOTE_DIR="/home/$EC2_USER/Load-Balancer-CPP"
SSH_OPTS="-i $SSH_KEY -o StrictHostKeyChecking=no"

echo "======================================================================"
echo " Deploying C++ Load Balancer to $REMOTE"
echo "======================================================================"

# ── 1. Sync source files ──────────────────────────────────────────────────────
echo ""
echo "==> Syncing source files to EC2..."
rsync -az --progress \
    --exclude='.git' \
    --exclude='dashboard/node_modules' \
    --exclude='dashboard/.next' \
    --exclude='load-balancer/load_balancer' \
    --exclude='load-balancer/*.o' \
    --exclude='server/echo_server' \
    --exclude='build/' \
    -e "ssh $SSH_OPTS" \
    "$ROOT/" \
    "$REMOTE:$REMOTE_DIR/"
echo "    Sync complete."

# ── 2. Build on EC2 ───────────────────────────────────────────────────────────
echo ""
echo "==> Building on EC2..."
ssh $SSH_OPTS "$REMOTE" bash <<ENDSSH
    set -e
    cd $REMOTE_DIR
    echo "    Building load_balancer..."
    (cd load-balancer && make -s load_balancer)
    echo "    Building echo_server..."
    (cd server        && make -s echo_server)
    echo "    Build complete."
ENDSSH

# ── 3. Restart systemd service ────────────────────────────────────────────────
echo ""
echo "==> Restarting lb service on EC2..."
ssh $SSH_OPTS "$REMOTE" "sudo systemctl restart lb 2>/dev/null || (cd $REMOTE_DIR && pkill load_balancer 2>/dev/null; pkill echo_server 2>/dev/null; sleep 1; bash scripts/start.sh &)"
sleep 2

# ── 4. Health check ───────────────────────────────────────────────────────────
echo ""
echo "==> Checking stats endpoint..."
if ssh $SSH_OPTS "$REMOTE" "curl -sf http://localhost:8081/stats" > /dev/null; then
    echo "    ✓ Stats endpoint is UP"
else
    echo "    ⚠ Stats endpoint not responding yet — check logs:"
    echo "      ssh -i $SSH_KEY $REMOTE 'journalctl -u lb -n 30'"
fi

echo ""
echo "======================================================================"
echo " Deploy complete!"
echo ""
echo " Stats API: http://$EC2_IP:8081/stats"
echo " Load LB:   http://$EC2_IP:8080/"
echo ""
echo " Test from local machine:"
echo "   curl http://$EC2_IP:8081/stats"
echo "   curl http://$EC2_IP:8080/"
echo ""
echo " Set dashboard endpoint to: http://$EC2_IP:8081"
echo "======================================================================"
