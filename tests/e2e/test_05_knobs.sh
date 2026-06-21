#!/usr/bin/env bash
# AWG settings matrix: runs the client+gateway topology (like test_02) for each
# of the five profiles that exercise distinct code paths in wg2awg.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

echo "--- test_05_knobs ---"

# Run the full wg -> proxy-client -> proxy-gateway -> wg topology with the given
# env vars on the two wg2awg containers.
# Usage: run_profile NAME SRV_PUB CLI_PUB PROXY_ENV...
run_profile() {
  local name="$1" srv_pub="$2" cli_pub="$3"
  shift 3
  local proxy_env=("$@")

  local net="wg2awg-e2e-05-${name}-${E2E_RUN_ID}"
  local subnet="172.28.5.0/24"
  local srv_ip="172.28.5.2"
  local gw_ip="172.28.5.3"
  local pc_ip="172.28.5.4"
  local cli_ip="172.28.5.5"

  local saved_containers="$E2E_CONTAINERS"
  local saved_networks="$E2E_NETWORKS"
  local saved_confdirs="$E2E_CONFDIRS"
  E2E_CONTAINERS=""
  E2E_NETWORKS=""
  E2E_CONFDIRS=""

  local profile_ok=0

  (
    trap e2e_cleanup EXIT

    local srv_dir cli_dir
    srv_dir=$(e2e_mkconfdir)
    e2e_write_wg_conf "$srv_dir" "$SRV_PRIV" 51820 \
      "$cli_pub" "" "10.99.5.2/32" >/dev/null

    cli_dir=$(e2e_mkconfdir)
    e2e_write_wg_conf "$cli_dir" "$CLI_PRIV" 0 \
      "$srv_pub" "${pc_ip}:51820" "10.99.5.1/32" 5 >/dev/null

    e2e_net_create "$net" "$subnet"

    e2e_start_wg "e2e-05-${name}-srv" "$net" "$srv_ip" "10.99.5.1/24" "$srv_dir"
    sleep 1
    e2e_start_proxy "e2e-05-${name}-gw" "$net" "$gw_ip" \
      gateway "${srv_ip}:51820" "$srv_pub" "$cli_pub" "${proxy_env[@]}"
    sleep 1
    e2e_start_proxy "e2e-05-${name}-pc" "$net" "$pc_ip" \
      client "${gw_ip}:51820" "$srv_pub" "$cli_pub" "${proxy_env[@]}"
    sleep 1
    e2e_start_wg "e2e-05-${name}-cli" "$net" "$cli_ip" "10.99.5.2/24" "$cli_dir"

    e2e_wait_handshake "e2e-05-${name}-cli" wg0 wg 20 ||
      e2e_fail "[$name] no handshake within 20 s"
    e2e_check_ping "e2e-05-${name}-cli" "10.99.5.1" ||
      e2e_fail "[$name] ping 10.99.5.1 failed"
    e2e_check_ping "e2e-05-${name}-srv" "10.99.5.2" ||
      e2e_fail "[$name] reverse ping 10.99.5.2 failed"
  )
  profile_ok=$?

  E2E_CONTAINERS="$saved_containers"
  E2E_NETWORKS="$saved_networks"
  E2E_CONFDIRS="$saved_confdirs"
  return $profile_ok
}

echo "  generating shared keypair for all profiles..."
SRV_KEYS=$(e2e_genkeys)
SRV_PRIV=$(echo "$SRV_KEYS" | head -1)
SRV_PUB=$(echo "$SRV_KEYS" | tail -1)

CLI_KEYS=$(e2e_genkeys)
CLI_PRIV=$(echo "$CLI_KEYS" | head -1)
CLI_PUB=$(echo "$CLI_KEYS" | tail -1)

# Generate morph key inside the image.
echo "  generating morph key..."
MORPH_KEY=$(docker run --rm "$E2E_IMAGE" wg2awg --gen-morph-key 2>/dev/null ||
  docker run --rm "$E2E_IMAGE" wg2awg -g 2>/dev/null ||
  echo "")
[ -z "$MORPH_KEY" ] && {
  echo "  SKIP morph: --gen-morph-key not supported"
  MORPH_KEY=""
}

PASS=0
FAIL=0

run_one() {
  local name="$1"
  shift
  echo "  --- profile: $name ---"
  if run_profile "$name" "$SRV_PUB" "$CLI_PUB" "$@"; then
    echo "  PASS: $name"
    PASS=$((PASS + 1))
  else
    echo "  FAIL: $name"
    FAIL=$((FAIL + 1))
  fi
}

# minimal: no junk, no padding, H values = 1/2/3/4 (minimum valid obfuscation)
run_one minimal \
  AWG_JC=0 AWG_S1=0 AWG_S2=0 \
  AWG_H1=1 AWG_H2=2 AWG_H3=3 AWG_H4=4

# v1_std: junk packets + S1/S2 padding + fixed magic headers
run_one v1_std \
  AWG_JC=4 AWG_JMIN=40 AWG_JMAX=70 \
  AWG_S1=20 AWG_S2=15 \
  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 AWG_H4=654563364

# v2_range: S3/S4 added, H values as ranges (exercises hrange_pick)
run_one v2_range \
  AWG_JC=3 AWG_JMIN=50 AWG_JMAX=150 \
  AWG_S1=10 AWG_S2=8 AWG_S3=5 AWG_S4=3 \
  AWG_H1="1000-2000" AWG_H2="3000-4000" AWG_H3="5000-6000" AWG_H4="7000-8000"

# morph: rotating H/S/J derived from key (fail-closed: no explicit H/S/J)
if [ -n "$MORPH_KEY" ]; then
  run_one morph AWG_MORPH_KEY="$MORPH_KEY"
else
  echo "  SKIP: morph (no morph key generated)"
fi

# cps: I1+I2 templates emitted before handshake-init
run_one cps \
  AWG_JC=2 AWG_JMIN=40 AWG_JMAX=70 \
  AWG_S1=20 AWG_S2=15 \
  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 AWG_H4=654563364 \
  "AWG_I1=<b 0x48656c6c6f><r 8><t>" \
  "AWG_I2=<rc 4><rd 4><c>"

# All outer obfuscation profiles (v1_std base params + each profile)
_OBFS_BASE_ENV=(
  AWG_JC=2 AWG_JMIN=50 AWG_JMAX=100
  AWG_S1=20 AWG_S2=15
  AWG_H1=1250212372 AWG_H2=322115822 AWG_H3=412530544 AWG_H4=654563364
)
for _profile in dtls_record stun_ice rtp_media source_query raknet quic_short game_enet game_kcp dns_like; do
  run_one "obfs_${_profile}" "${_OBFS_BASE_ENV[@]}" "AWG_OBFS_PROFILE=${_profile}"
done

echo ""
echo "  knobs results: $PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ] || {
  echo "test_05_knobs: FAIL"
  exit 1
}
echo "test_05_knobs: PASS"
