#!/usr/bin/env bash
# Compatibility: wg-client -> wg2awg(client) -> amneziawg-go server.
# Verifies that wg2awg produces AWG-protocol packets that a real AWG server accepts.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
trap e2e_cleanup EXIT

echo "--- test_03_awg_server ---"

NET="wg2awg-e2e-03-${E2E_RUN_ID}"
SUBNET="172.28.3.0/24"
SRV_IP="172.28.3.2"
PC_IP="172.28.3.3"
CLI_IP="172.28.3.4"
SRV_TUN="10.99.3.1/24"
CLI_TUN="10.99.3.2/24"
SRV_TUN_HOST="10.99.3.1"

# Shared AWG parameters (must match between wg2awg client and amneziawg-go server).
AWG_JC=4
AWG_JMIN=40
AWG_JMAX=70
AWG_S1=20
AWG_S2=15
AWG_H1=1250212372
AWG_H2=322115822
AWG_H3=412530544
AWG_H4=654563364

AWG_CONF_PARAMS="Jc = $AWG_JC
Jmin = $AWG_JMIN
Jmax = $AWG_JMAX
S1 = $AWG_S1
S2 = $AWG_S2
H1 = $AWG_H1
H2 = $AWG_H2
H3 = $AWG_H3
H4 = $AWG_H4"

echo "  generating keys..."
# AWG server keys (used with amneziawg-go).
AWG_SRV_KEYS=$(e2e_genkeys)
AWG_SRV_PRIV=$(echo "$AWG_SRV_KEYS" | head -1)
AWG_SRV_PUB=$(echo "$AWG_SRV_KEYS" | tail -1)

# WG client keys (plain wireguard-go).
WG_CLI_KEYS=$(e2e_genkeys)
WG_CLI_PRIV=$(echo "$WG_CLI_KEYS" | head -1)
WG_CLI_PUB=$(echo "$WG_CLI_KEYS" | tail -1)

# amneziawg-go server config.
SRV_DIR=$(e2e_mkconfdir)
e2e_write_awg_conf "$SRV_DIR" "$AWG_SRV_PRIV" 51820 "$AWG_CONF_PARAMS" \
  "$WG_CLI_PUB" "" "10.99.3.2/32" >/dev/null

# WG client config: endpoint points at wg2awg proxy-client.
CLI_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$CLI_DIR" "$WG_CLI_PRIV" 0 \
  "$AWG_SRV_PUB" "${PC_IP}:51820" "10.99.3.1/32" 5 >/dev/null

echo "  creating network $NET..."
e2e_net_create "$NET" "$SUBNET"

echo "  starting amneziawg-go server..."
e2e_start_awg "e2e-03-awg-server" "$NET" "$SRV_IP" "$SRV_TUN" "$SRV_DIR"
sleep 1

echo "  starting proxy-client (mode=client, remote=awg-server)..."
e2e_start_proxy "e2e-03-proxy-client" "$NET" "$PC_IP" \
  client "${SRV_IP}:51820" "$AWG_SRV_PUB" "$WG_CLI_PUB" \
  AWG_JC="$AWG_JC" AWG_JMIN="$AWG_JMIN" AWG_JMAX="$AWG_JMAX" \
  AWG_S1="$AWG_S1" AWG_S2="$AWG_S2" \
  AWG_H1="$AWG_H1" AWG_H2="$AWG_H2" AWG_H3="$AWG_H3" AWG_H4="$AWG_H4"
sleep 1

echo "  starting wg-client..."
e2e_start_wg "e2e-03-wg-client" "$NET" "$CLI_IP" "$CLI_TUN" "$CLI_DIR"

echo "  waiting for handshake (up to 20 s)..."
e2e_wait_handshake "e2e-03-wg-client" wg0 wg 20 ||
  e2e_fail "no WireGuard handshake via wg2awg -> amneziawg-go within 20 s"
echo "  handshake OK"

echo "  pinging AWG server tunnel IP from client..."
e2e_check_ping "e2e-03-wg-client" "$SRV_TUN_HOST" ||
  e2e_fail "ping $SRV_TUN_HOST from wg-client failed"
echo "  ping OK"

echo "test_03_awg_server: PASS"
