#!/usr/bin/env bash
# Core wg2awg test: wg-client -> wg2awg(client) -> wg2awg(gateway) -> wg-server.
# Uses v1_std AWG profile: Jc=4, Jmin=40, Jmax=70, S1=20, S2=15, fixed H1-H4.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
trap e2e_cleanup EXIT

echo "--- test_02_client_gw ---"

NET="wg2awg-e2e-02-${E2E_RUN_ID}"
SUBNET="172.28.2.0/24"
SRV_IP="172.28.2.2"
GW_IP="172.28.2.3"
PC_IP="172.28.2.4"
CLI_IP="172.28.2.5"
SRV_TUN="10.99.2.1/24"
CLI_TUN="10.99.2.2/24"
SRV_TUN_HOST="10.99.2.1"
CLI_TUN_HOST="10.99.2.2"

# v1_std AWG profile env vars (same on both proxy sides).
AWG_PROFILE=(
  AWG_JC=4
  AWG_JMIN=40
  AWG_JMAX=70
  AWG_S1=20
  AWG_S2=15
  AWG_H1=1250212372
  AWG_H2=322115822
  AWG_H3=412530544
  AWG_H4=654563364
)

echo "  generating keys..."
SRV_KEYS=$(e2e_genkeys)
SRV_PRIV=$(echo "$SRV_KEYS" | head -1)
SRV_PUB=$(echo "$SRV_KEYS" | tail -1)

CLI_KEYS=$(e2e_genkeys)
CLI_PRIV=$(echo "$CLI_KEYS" | head -1)
CLI_PUB=$(echo "$CLI_KEYS" | tail -1)

# WG server config: accepts client's key, no endpoint (client initiates).
SRV_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$SRV_DIR" "$SRV_PRIV" 51820 \
  "$CLI_PUB" "" "10.99.2.2/32" >/dev/null

# WG client config: endpoint points at proxy-client (not at the real server).
CLI_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$CLI_DIR" "$CLI_PRIV" 0 \
  "$SRV_PUB" "${PC_IP}:51820" "10.99.2.1/32" 5 >/dev/null

echo "  creating network $NET..."
e2e_net_create "$NET" "$SUBNET"

# Start in order: server -> gateway -> client-proxy -> wg-client.
echo "  starting wg-server..."
e2e_start_wg "e2e-02-wg-server" "$NET" "$SRV_IP" "$SRV_TUN" "$SRV_DIR"
sleep 1

echo "  starting proxy-gateway (mode=gateway, remote=wg-server)..."
e2e_start_proxy "e2e-02-proxy-gw" "$NET" "$GW_IP" \
  gateway "${SRV_IP}:51820" "$SRV_PUB" "$CLI_PUB" \
  "${AWG_PROFILE[@]}"
sleep 1

echo "  starting proxy-client (mode=client, remote=proxy-gw)..."
e2e_start_proxy "e2e-02-proxy-client" "$NET" "$PC_IP" \
  client "${GW_IP}:51820" "$SRV_PUB" "$CLI_PUB" \
  "${AWG_PROFILE[@]}"
sleep 1

echo "  starting wg-client..."
e2e_start_wg "e2e-02-wg-client" "$NET" "$CLI_IP" "$CLI_TUN" "$CLI_DIR"

# Wait for handshake (allow 20 s — wg2awg adds junk packets before init).
echo "  waiting for handshake (up to 20 s)..."
e2e_wait_handshake "e2e-02-wg-client" wg0 wg 20 ||
  e2e_fail "no WireGuard handshake via wg2awg client+gateway within 20 s"
echo "  handshake OK"

# Bidirectional ping.
echo "  pinging server tunnel IP from client..."
e2e_check_ping "e2e-02-wg-client" "$SRV_TUN_HOST" ||
  e2e_fail "ping server ($SRV_TUN_HOST) from wg-client failed"
echo "  client -> server ping OK"

echo "  pinging client tunnel IP from server..."
e2e_check_ping "e2e-02-wg-server" "$CLI_TUN_HOST" ||
  e2e_fail "ping client ($CLI_TUN_HOST) from wg-server failed"
echo "  server -> client ping OK"

echo "test_02_client_gw: PASS"
