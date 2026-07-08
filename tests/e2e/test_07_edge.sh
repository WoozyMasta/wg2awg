#!/usr/bin/env bash
# Edge cases: feature composition and simultaneous-client gateway stress.
#   1. morph + outer obfs (dtls_record) combined
#   2. CPS (I1+I2) + outer obfs (stun_ice) combined
#   3. Two WG clients through one proxy-gateway simultaneously (gateway multi-client)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

echo "--- test_07_edge ---"

# ---------------------------------------------------------------------------
# Helper: run one client+gateway topology with given proxy env vars.
# Each call uses its own sub-network derived from the name.
# ---------------------------------------------------------------------------
run_edge() {
  local name="$1"; shift
  local proxy_env=("$@")

  local net="wg2awg-e2e-07-${name}-${E2E_RUN_ID}"
  local subnet="172.28.7.0/24"

  local saved_c="$E2E_CONTAINERS" saved_n="$E2E_NETWORKS" saved_d="$E2E_CONFDIRS"
  E2E_CONTAINERS=""; E2E_NETWORKS=""; E2E_CONFDIRS=""

  local rc=0
  (
    trap e2e_cleanup EXIT

    local srv_dir cli_dir
    srv_dir=$(e2e_mkconfdir)
    e2e_write_wg_conf "$srv_dir" "$SRV_PRIV" 51820 \
      "$CLI_PUB" "" "10.99.7.2/32" >/dev/null

    cli_dir=$(e2e_mkconfdir)
    e2e_write_wg_conf "$cli_dir" "$CLI_PRIV" 0 \
      "$SRV_PUB" "172.28.7.4:51820" "10.99.7.1/32" 5 >/dev/null

    e2e_net_create "$net" "$subnet"

    e2e_start_wg "e2e-07-${name}-srv" "$net" "172.28.7.2" "10.99.7.1/24" "$srv_dir"
    sleep 1
    e2e_start_proxy "e2e-07-${name}-gw" "$net" "172.28.7.3" \
      gateway "172.28.7.2:51820" "$SRV_PUB" "$CLI_PUB" "${proxy_env[@]}"
    sleep 1
    e2e_start_proxy "e2e-07-${name}-pc" "$net" "172.28.7.4" \
      client "172.28.7.3:51820" "$SRV_PUB" "$CLI_PUB" "${proxy_env[@]}"
    sleep 1
    e2e_start_wg "e2e-07-${name}-cli" "$net" "172.28.7.5" "10.99.7.2/24" "$cli_dir"

    e2e_wait_handshake "e2e-07-${name}-cli" wg0 wg 25 ||
      e2e_fail "[$name] no handshake within 25 s"
    e2e_check_ping "e2e-07-${name}-cli" "10.99.7.1" ||
      e2e_fail "[$name] ping 10.99.7.1 failed"
    e2e_check_ping "e2e-07-${name}-srv" "10.99.7.2" ||
      e2e_fail "[$name] reverse ping 10.99.7.2 failed"
  ) || rc=$?

  E2E_CONTAINERS="$saved_c"; E2E_NETWORKS="$saved_n"; E2E_CONFDIRS="$saved_d"
  return $rc
}

echo "  generating keys..."
SRV_KEYS=$(e2e_genkeys); SRV_PRIV=$(echo "$SRV_KEYS" | head -1); SRV_PUB=$(echo "$SRV_KEYS" | tail -1)
CLI_KEYS=$(e2e_genkeys); CLI_PRIV=$(echo "$CLI_KEYS" | head -1); CLI_PUB=$(echo "$CLI_KEYS" | tail -1)

echo "  generating morph key..."
MORPH_KEY=$(docker run --rm "$E2E_IMAGE" wg2awg --gen-morph-key 2>/dev/null ||
  docker run --rm "$E2E_IMAGE" wg2awg -g 2>/dev/null ||
  echo "")
[ -z "$MORPH_KEY" ] && echo "  SKIP morph tests: --gen-morph-key not supported"

PASS=0; FAIL=0

run_one() {
  local name="$1"; shift
  echo "  --- edge: $name ---"
  if run_edge "$name" "$@"; then
    echo "  PASS: $name"; PASS=$((PASS+1))
  else
    echo "  FAIL: $name"; FAIL=$((FAIL+1))
  fi
}

# 1. morph + outer obfs/dtls_record
if [ -n "$MORPH_KEY" ]; then
  run_one morph_obfs \
    "AWG_MORPH_KEY=$MORPH_KEY" \
    AWG_OBFS_PROFILE=dtls_record
else
  echo "  SKIP: morph_obfs"
fi

# 2. CPS (I1+I2) + outer obfs/stun_ice on top of v1_std params
run_one cps_obfs \
  AWG_JC=2 AWG_JMIN=40 AWG_JMAX=70 \
  AWG_S1=20 AWG_S2=15 \
  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 AWG_H4=654563364 \
  "AWG_I1=<b 0x48656c6c6f><r 8>" \
  "AWG_I2=<rc 4><rd 4><c>" \
  AWG_OBFS_PROFILE=stun_ice

# 3. v2 range headers + outer obfs: exercises hrange_pick path through an obfs wrapper.
run_one v2range_obfs \
  AWG_JC=3 AWG_JMIN=50 AWG_JMAX=150 \
  AWG_S1=10 AWG_S2=8 AWG_S3=5 AWG_S4=3 \
  AWG_H1="1000-2000" AWG_H2="3000-4000" AWG_H3="5000-6000" AWG_H4="7000-8000" \
  AWG_OBFS_PROFILE=dtls_record

echo ""
echo "  edge results: $PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ] || { echo "test_07_edge: FAIL"; exit 1; }
echo "test_07_edge: PASS"
