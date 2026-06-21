#!/usr/bin/env bash
# Shared helpers for wg2awg e2e tests.
# Source this file; do not execute directly.

E2E_IMAGE=${E2E_IMAGE:-wg2awg-e2e}
E2E_CONTAINERS=""
E2E_NETWORKS=""
E2E_CONFDIRS=""

# Prevent Git Bash (MSYS) from converting Linux paths to Windows paths
# in Docker command arguments (e.g. /tmp -> C:\Users\...\Temp).
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

# Unique run ID so parallel test invocations don't collide.
E2E_RUN_ID=${E2E_RUN_ID:-$(date +%s | tail -c 6)}

# Cleanup
# ---------------------------------------------------------------------------

e2e_cleanup() {
  local c n d
  for c in $E2E_CONTAINERS; do
    docker rm -f "$c" >/dev/null 2>&1 || true
  done
  for n in $E2E_NETWORKS; do
    docker network rm "$n" >/dev/null 2>&1 || true
  done
  for d in $E2E_CONFDIRS; do
    rm -rf "$d" 2>/dev/null || true
  done
}

e2e_fail() {
  echo "FAIL: $1" >&2
  echo "--- container logs ---" >&2
  local c
  for c in $E2E_CONTAINERS; do
    echo "==> $c" >&2
    docker logs "$c" 2>&1 | tail -50 >&2 || true
  done
  e2e_cleanup
  exit 1
}

# Key generation (runs inside the image to avoid host dependency on wg)
# ---------------------------------------------------------------------------

# Outputs two lines: PRIVATE_KEY then PUBLIC_KEY
e2e_genkeys() {
  local priv pub
  priv=$(docker run --rm "$E2E_IMAGE" sh -c 'wg genkey')
  pub=$(docker run --rm "$E2E_IMAGE" sh -c "echo '$priv' | wg pubkey")
  printf '%s\n%s\n' "$priv" "$pub"
}

# Network management
# ---------------------------------------------------------------------------

e2e_net_create() {
  local name="$1" subnet="$2"
  docker network create --subnet "$subnet" "$name" >/dev/null
  E2E_NETWORKS="$E2E_NETWORKS $name"
}

e2e_net_delete() {
  docker network rm "$1" >/dev/null 2>&1 || true
}

# Temp config directory
# ---------------------------------------------------------------------------

e2e_mkconfdir() {
  local d
  d=$(mktemp -d)
  E2E_CONFDIRS="$E2E_CONFDIRS $d"
  echo "$d"
}

# Config file writers
# ---------------------------------------------------------------------------

# Write a plain WireGuard config.
# Usage: e2e_write_wg_conf DIR PRIV LISTEN_PORT \
#            [PEER_PUB PEER_ENDPOINT PEER_ALLOWED_IPS [KEEPALIVE]]
e2e_write_wg_conf() {
  local dir="$1" priv="$2" port="$3"
  local peer_pub="${4:-}" peer_ep="${5:-}" peer_allowed="${6:-}" keepalive="${7:-}"
  local f="$dir/wg0.conf"
  cat >"$f" <<EOF
[Interface]
PrivateKey = $priv
ListenPort = $port
EOF
  if [ -n "$peer_pub" ]; then
    cat >>"$f" <<EOF

[Peer]
PublicKey = $peer_pub
AllowedIPs = $peer_allowed
EOF
    [ -n "$peer_ep" ] && echo "Endpoint = $peer_ep" >>"$f"
    [ -n "$keepalive" ] && echo "PersistentKeepalive = $keepalive" >>"$f"
  fi
  echo "$f"
}

# Write an AmneziaWG config (same as WG plus AWG fields in [Interface]).
# AWG_PARAMS is a newline-separated list of "KEY = VALUE" lines.
# Usage: e2e_write_awg_conf DIR PRIV LISTEN_PORT AWG_PARAMS \
#            [PEER_PUB PEER_ENDPOINT PEER_ALLOWED_IPS [KEEPALIVE]]
e2e_write_awg_conf() {
  local dir="$1" priv="$2" port="$3" awg_params="$4"
  local peer_pub="${5:-}" peer_ep="${6:-}" peer_allowed="${7:-}" keepalive="${8:-}"
  local f="$dir/awg0.conf"
  cat >"$f" <<EOF
[Interface]
PrivateKey = $priv
ListenPort = $port
$awg_params
EOF
  if [ -n "$peer_pub" ]; then
    cat >>"$f" <<EOF

[Peer]
PublicKey = $peer_pub
AllowedIPs = $peer_allowed
EOF
    [ -n "$peer_ep" ] && echo "Endpoint = $peer_ep" >>"$f"
    [ -n "$keepalive" ] && echo "PersistentKeepalive = $keepalive" >>"$f"
  fi
  echo "$f"
}

# Container launchers
# ---------------------------------------------------------------------------

# Start a wireguard-go container.
# Config is injected via docker exec after startup (avoids host volume mount path issues on Windows).
# Usage: e2e_start_wg CNAME NETNAME CONTAINER_IP TUN_CIDR CONFDIR
e2e_start_wg() {
  local cname="$1" net="$2" ip="$3" tun_cidr="$4" confdir="$5"
  docker run -d \
    --name "$cname" \
    --network "$net" \
    --ip "$ip" \
    --cap-add NET_ADMIN \
    --cap-add MKNOD \
    --sysctl net.ipv4.ip_forward=1 \
    --entrypoint sh \
    "$E2E_IMAGE" \
    -c "ip link add wg0 type wireguard 2>/dev/null || {
          mkdir -p /dev/net; [ -c /dev/net/tun ] || mknod -m 0666 /dev/net/tun c 10 200
          wireguard-go wg0
          i=0; while ! ip link show wg0 >/dev/null 2>&1; do sleep 0.1; i=\$((i+1)); [ \$i -gt 50 ] && exit 1; done
        }
        ip link set wg0 up
        ip addr add $tun_cidr dev wg0
        touch /tmp/iface_ready
        tail -f /dev/null" >/dev/null
  E2E_CONTAINERS="$E2E_CONTAINERS $cname"
  # Wait for interface, then inject config via stdin (no volume mount needed).
  local i=0
  until docker exec "$cname" test -f /tmp/iface_ready 2>/dev/null; do
    sleep 0.2; i=$((i+1)); [ $i -gt 50 ] && e2e_fail "$cname: wg0 interface did not come up"
  done
  docker exec -i "$cname" wg setconf wg0 /dev/stdin < "$confdir/wg0.conf"
}

# Start an amneziawg-go container.
# Config is injected via docker exec after startup (avoids host volume mount path issues on Windows).
# Usage: e2e_start_awg CNAME NETNAME CONTAINER_IP TUN_CIDR CONFDIR
e2e_start_awg() {
  local cname="$1" net="$2" ip="$3" tun_cidr="$4" confdir="$5"
  docker run -d \
    --name "$cname" \
    --network "$net" \
    --ip "$ip" \
    --cap-add NET_ADMIN \
    --cap-add MKNOD \
    --sysctl net.ipv4.ip_forward=1 \
    --entrypoint sh \
    "$E2E_IMAGE" \
    -c "mkdir -p /dev/net; [ -c /dev/net/tun ] || mknod -m 0666 /dev/net/tun c 10 200
        amneziawg-go awg0
        i=0; while ! ip link show awg0 >/dev/null 2>&1 || ip link show awg0 2>/dev/null | grep -q 'ERROR'; do
          sleep 0.1; i=\$((i+1)); [ \$i -gt 100 ] && exit 1
        done
        ip link set awg0 up
        ip addr add $tun_cidr dev awg0
        touch /tmp/iface_ready
        tail -f /dev/null" >/dev/null
  E2E_CONTAINERS="$E2E_CONTAINERS $cname"
  local i=0
  until docker exec "$cname" test -f /tmp/iface_ready 2>/dev/null; do
    sleep 0.2; i=$((i+1)); [ $i -gt 50 ] && e2e_fail "$cname: awg0 interface did not come up"
  done
  docker exec -i "$cname" awg setconf awg0 /dev/stdin < "$confdir/awg0.conf"
}

# Start a wg2awg container (no TUN/NET_ADMIN needed).
# Extra env vars passed as "KEY=VALUE" positional args after CLIENT_PUB.
# Usage: e2e_start_proxy CNAME NETNAME IP MODE REMOTE SERVER_PUB CLIENT_PUB [KEY=VAL ...]
e2e_start_proxy() {
  local cname="$1" net="$2" ip="$3" mode="$4" remote="$5"
  local server_pub="$6" client_pub="$7"
  shift 7
  # Build -e flags for extra env vars
  local extra_env=()
  local kv
  for kv in "$@"; do
    extra_env+=(-e "$kv")
  done
  docker run -d \
    --name "$cname" \
    --network "$net" \
    --ip "$ip" \
    -e AWG_MODE="$mode" \
    -e AWG_LISTEN=":51820" \
    -e AWG_REMOTE="$remote" \
    -e AWG_SERVER_PUB="$server_pub" \
    -e AWG_CLIENT_PUB="$client_pub" \
    -e AWG_LOG_LEVEL=debug \
    -e AWG_TIMEOUT=30 \
    -e AWG_NO_GRO=1 \
    "${extra_env[@]}" \
    "$E2E_IMAGE" \
    wg2awg >/dev/null
  E2E_CONTAINERS="$E2E_CONTAINERS $cname"
}

# Checks
# ---------------------------------------------------------------------------

# Wait for a WireGuard handshake to appear.
# Polls "wg_cmd show IFACE latest-handshakes" until a real unix timestamp is seen.
# Usage: e2e_wait_handshake CNAME IFACE [WG_CMD=wg] [TIMEOUT_S=15]
e2e_wait_handshake() {
  local cname="$1" iface="$2" wg_cmd="${3:-wg}" timeout="${4:-15}"
  local deadline=$(($(date +%s) + timeout))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if docker exec "$cname" "$wg_cmd" show "$iface" latest-handshakes 2>/dev/null |
      awk '{print $2}' |
      grep -qxE '[1-9][0-9]{9,}'; then
      return 0
    fi
    sleep 1
  done
  return 1
}

# Run ping from a container to a target IP.
# Usage: e2e_check_ping CNAME TARGET_IP [COUNT=3]
e2e_check_ping() {
  local cname="$1" target="$2" count="${3:-3}"
  docker exec "$cname" ping -c "$count" -W 2 "$target" >/dev/null 2>&1
}
