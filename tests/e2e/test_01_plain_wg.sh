#!/usr/bin/env bash
# Baseline: wireguard-go client <-> wireguard-go server, no wg2awg involved.
# Verifies that WireGuard userspace tunnels work inside containers at all.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
trap e2e_cleanup EXIT

echo "--- test_01_plain_wg ---"

NET="wg2awg-e2e-01-${E2E_RUN_ID}"
SUBNET="172.28.1.0/24"
SRV_IP="172.28.1.2"
CLI_IP="172.28.1.3"
SRV_TUN="10.99.1.1/24"
CLI_TUN="10.99.1.2/24"
SRV_TUN_HOST="10.99.1.1"

# Generate keypairs.
echo "  generating keys..."
SRV_KEYS=$(e2e_genkeys)
SRV_PRIV=$(echo "$SRV_KEYS" | head -1)
SRV_PUB=$(echo "$SRV_KEYS" | tail -1)

CLI_KEYS=$(e2e_genkeys)
CLI_PRIV=$(echo "$CLI_KEYS" | head -1)
CLI_PUB=$(echo "$CLI_KEYS" | tail -1)

# Write configs.
SRV_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$SRV_DIR" "$SRV_PRIV" 51820 \
  "$CLI_PUB" "" "10.99.1.2/32" >/dev/null

CLI_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$CLI_DIR" "$CLI_PRIV" 0 \
  "$SRV_PUB" "${SRV_IP}:51820" "10.99.1.1/32" 5 >/dev/null

# Create network and start containers.
echo "  creating network $NET..."
e2e_net_create "$NET" "$SUBNET"

echo "  starting wg-server..."
e2e_start_wg "e2e-01-wg-server" "$NET" "$SRV_IP" "$SRV_TUN" "$SRV_DIR"
sleep 1

echo "  starting wg-client..."
e2e_start_wg "e2e-01-wg-client" "$NET" "$CLI_IP" "$CLI_TUN" "$CLI_DIR"

# Check handshake (client side, up to 15 s).
echo "  waiting for handshake..."
e2e_wait_handshake "e2e-01-wg-client" wg0 wg 15 ||
  e2e_fail "no WireGuard handshake within 15 s"
echo "  handshake OK"

# Verify bidirectional ping.
echo "  pinging server tunnel IP from client..."
e2e_check_ping "e2e-01-wg-client" "$SRV_TUN_HOST" ||
  e2e_fail "ping $SRV_TUN_HOST from client failed"
echo "  ping OK"

echo "test_01_plain_wg: PASS"
