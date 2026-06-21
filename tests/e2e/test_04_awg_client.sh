#!/usr/bin/env bash
# Compatibility: amneziawg-go client -> wg2awg(gateway) -> wg-server.
# Verifies that wg2awg gateway correctly strips AWG framing for a plain WG backend.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
trap e2e_cleanup EXIT

echo "--- test_04_awg_client ---"

NET="wg2awg-e2e-04-${E2E_RUN_ID}"
SUBNET="172.28.4.0/24"
SRV_IP="172.28.4.2"
GW_IP="172.28.4.3"
CLI_IP="172.28.4.4"
SRV_TUN="10.99.4.1/24"
CLI_TUN="10.99.4.2/24"
SRV_TUN_HOST="10.99.4.1"

# Shared AWG parameters.
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
WG_SRV_KEYS=$(e2e_genkeys)
WG_SRV_PRIV=$(echo "$WG_SRV_KEYS" | head -1)
WG_SRV_PUB=$(echo "$WG_SRV_KEYS" | tail -1)

AWG_CLI_KEYS=$(e2e_genkeys)
AWG_CLI_PRIV=$(echo "$AWG_CLI_KEYS" | head -1)
AWG_CLI_PUB=$(echo "$AWG_CLI_KEYS" | tail -1)

# Plain WG server config: sees AWG client's key (wg2awg passes keys through).
SRV_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$SRV_DIR" "$WG_SRV_PRIV" 51820 \
  "$AWG_CLI_PUB" "" "10.99.4.2/32" >/dev/null

# amneziawg-go client config: endpoint points at wg2awg gateway.
CLI_DIR=$(e2e_mkconfdir)
e2e_write_awg_conf "$CLI_DIR" "$AWG_CLI_PRIV" 0 "$AWG_CONF_PARAMS" \
  "$WG_SRV_PUB" "${GW_IP}:51820" "10.99.4.1/32" 5 >/dev/null

echo "  creating network $NET..."
e2e_net_create "$NET" "$SUBNET"

echo "  starting wg-server..."
e2e_start_wg "e2e-04-wg-server" "$NET" "$SRV_IP" "$SRV_TUN" "$SRV_DIR"
sleep 1

echo "  starting proxy-gateway (mode=gateway, remote=wg-server)..."
e2e_start_proxy "e2e-04-proxy-gw" "$NET" "$GW_IP" \
  gateway "${SRV_IP}:51820" "$WG_SRV_PUB" "$AWG_CLI_PUB" \
  AWG_JC="$AWG_JC" AWG_JMIN="$AWG_JMIN" AWG_JMAX="$AWG_JMAX" \
  AWG_S1="$AWG_S1" AWG_S2="$AWG_S2" \
  AWG_H1="$AWG_H1" AWG_H2="$AWG_H2" AWG_H3="$AWG_H3" AWG_H4="$AWG_H4"
sleep 1

echo "  starting amneziawg-go client..."
e2e_start_awg "e2e-04-awg-client" "$NET" "$CLI_IP" "$CLI_TUN" "$CLI_DIR"

echo "  waiting for handshake (up to 20 s)..."
e2e_wait_handshake "e2e-04-awg-client" awg0 awg 20 ||
  e2e_fail "no WireGuard handshake via amneziawg-go -> wg2awg gateway within 20 s"
echo "  handshake OK"

echo "  pinging WG server tunnel IP from AWG client..."
e2e_check_ping "e2e-04-awg-client" "$SRV_TUN_HOST" ||
  e2e_fail "ping $SRV_TUN_HOST from awg-client failed"
echo "  ping OK"

echo "test_04_awg_client: PASS"
