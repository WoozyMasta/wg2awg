#!/usr/bin/env bash
# Server mode (1:N): two AWG proxy-clients sharing one wg2awg(server) → wg-server.
# Verifies per-client session demultiplexing (sender/receiver index table).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
trap e2e_cleanup EXIT

echo "--- test_06_server ---"

NET="wg2awg-e2e-06-${E2E_RUN_ID}"
SUBNET="172.28.6.0/24"
SRV_IP="172.28.6.2"
PROXY_SRV_IP="172.28.6.3"
PC1_IP="172.28.6.4"
PC2_IP="172.28.6.5"
CLI1_IP="172.28.6.6"
CLI2_IP="172.28.6.7"

# Common AWG obfuscation params for all proxies
COMMON_ENV=(
  -e AWG_JC=4 -e AWG_JMIN=40 -e AWG_JMAX=70
  -e AWG_S1=20 -e AWG_S2=15
  -e AWG_H1=1250212372 -e AWG_H2=322115822 -e AWG_H3=412530544 -e AWG_H4=654563364
  -e AWG_LOG_LEVEL=debug -e AWG_TIMEOUT=30 -e AWG_NO_GRO=1
)

echo "  generating keys..."
SRV_KEYS=$(e2e_genkeys); SRV_PRIV=$(echo "$SRV_KEYS" | head -1); SRV_PUB=$(echo "$SRV_KEYS" | tail -1)
CLI1_KEYS=$(e2e_genkeys); CLI1_PRIV=$(echo "$CLI1_KEYS" | head -1); CLI1_PUB=$(echo "$CLI1_KEYS" | tail -1)
CLI2_KEYS=$(e2e_genkeys); CLI2_PRIV=$(echo "$CLI2_KEYS" | head -1); CLI2_PUB=$(echo "$CLI2_KEYS" | tail -1)

# WG server config: two peers
SRV_DIR=$(e2e_mkconfdir)
cat >"$SRV_DIR/wg0.conf" <<EOF
[Interface]
PrivateKey = $SRV_PRIV
ListenPort = 51820

[Peer]
PublicKey = $CLI1_PUB
AllowedIPs = 10.99.6.2/32

[Peer]
PublicKey = $CLI2_PUB
AllowedIPs = 10.99.6.3/32
EOF

# WG client configs — each points to its own proxy-client
CLI1_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$CLI1_DIR" "$CLI1_PRIV" 0 \
  "$SRV_PUB" "${PC1_IP}:51820" "10.99.6.1/32" 5 >/dev/null

CLI2_DIR=$(e2e_mkconfdir)
e2e_write_wg_conf "$CLI2_DIR" "$CLI2_PRIV" 0 \
  "$SRV_PUB" "${PC2_IP}:51820" "10.99.6.1/32" 5 >/dev/null

echo "  creating network $NET..."
e2e_net_create "$NET" "$SUBNET"

echo "  starting wg-server (2 peers)..."
e2e_start_wg "e2e-06-wg-server" "$NET" "$SRV_IP" "10.99.6.1/24" "$SRV_DIR"
sleep 1

echo "  starting wg2awg server (mode=server, 2 client keys)..."
docker run -d \
  --name "e2e-06-proxy-srv" \
  --network "$NET" \
  --ip "$PROXY_SRV_IP" \
  -e AWG_MODE=server \
  -e AWG_LISTEN=":51820" \
  -e AWG_REMOTE="${SRV_IP}:51820" \
  -e AWG_SERVER_PUB="$SRV_PUB" \
  -e "AWG_CLIENT_PUBS=${CLI1_PUB},${CLI2_PUB}" \
  "${COMMON_ENV[@]}" \
  "$E2E_IMAGE" wg2awg >/dev/null
E2E_CONTAINERS="$E2E_CONTAINERS e2e-06-proxy-srv"
sleep 1

echo "  starting proxy-client1..."
docker run -d \
  --name "e2e-06-pc1" \
  --network "$NET" \
  --ip "$PC1_IP" \
  -e AWG_MODE=client \
  -e AWG_LISTEN=":51820" \
  -e AWG_REMOTE="${PROXY_SRV_IP}:51820" \
  -e AWG_SERVER_PUB="$SRV_PUB" \
  -e AWG_CLIENT_PUB="$CLI1_PUB" \
  -e AWG_SRC_PORT=11820 \
  "${COMMON_ENV[@]}" \
  "$E2E_IMAGE" wg2awg >/dev/null
E2E_CONTAINERS="$E2E_CONTAINERS e2e-06-pc1"
sleep 1

echo "  starting proxy-client2..."
docker run -d \
  --name "e2e-06-pc2" \
  --network "$NET" \
  --ip "$PC2_IP" \
  -e AWG_MODE=client \
  -e AWG_LISTEN=":51820" \
  -e AWG_REMOTE="${PROXY_SRV_IP}:51820" \
  -e AWG_SERVER_PUB="$SRV_PUB" \
  -e AWG_CLIENT_PUB="$CLI2_PUB" \
  -e AWG_SRC_PORT=11820 \
  "${COMMON_ENV[@]}" \
  "$E2E_IMAGE" wg2awg >/dev/null
E2E_CONTAINERS="$E2E_CONTAINERS e2e-06-pc2"
sleep 1

echo "  starting wg-client1..."
e2e_start_wg "e2e-06-wg-cli1" "$NET" "$CLI1_IP" "10.99.6.2/24" "$CLI1_DIR"

echo "  starting wg-client2..."
e2e_start_wg "e2e-06-wg-cli2" "$NET" "$CLI2_IP" "10.99.6.3/24" "$CLI2_DIR"

echo "  waiting for client1 handshake (up to 25 s)..."
e2e_wait_handshake "e2e-06-wg-cli1" wg0 wg 25 ||
  e2e_fail "client1: no handshake within 25 s"
echo "  client1 handshake OK"

echo "  waiting for client2 handshake (up to 25 s)..."
e2e_wait_handshake "e2e-06-wg-cli2" wg0 wg 25 ||
  e2e_fail "client2: no handshake within 25 s"
echo "  client2 handshake OK"

# Verify both forward and reverse directions work for both clients.
echo "  client1 -> server ping..."
e2e_check_ping "e2e-06-wg-cli1" "10.99.6.1" || e2e_fail "client1 -> server ping failed"
echo "  client1 -> server ping OK"

echo "  client2 -> server ping..."
e2e_check_ping "e2e-06-wg-cli2" "10.99.6.1" || e2e_fail "client2 -> server ping failed"
echo "  client2 -> server ping OK"

echo "  server -> client1 ping (reverse demux check)..."
e2e_check_ping "e2e-06-wg-server" "10.99.6.2" || e2e_fail "server -> client1 ping failed"
echo "  server -> client1 ping OK"

echo "  server -> client2 ping (reverse demux check)..."
e2e_check_ping "e2e-06-wg-server" "10.99.6.3" || e2e_fail "server -> client2 ping failed"
echo "  server -> client2 ping OK"

echo "test_06_server: PASS"
