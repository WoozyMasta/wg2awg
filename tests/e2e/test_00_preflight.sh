#!/usr/bin/env bash
# Preflight checks: verify the host environment can run e2e tests.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

ok() { echo "  OK: $*"; }
fail() {
  echo "FAIL: $*" >&2
  exit 1
}

echo "--- preflight ---"

# 1. Docker daemon is reachable.
docker info >/dev/null 2>&1 || fail "docker daemon not reachable"
ok "docker daemon"

# 2. Test image exists and runs.
docker run --rm "$E2E_IMAGE" echo ok >/dev/null 2>&1 ||
  fail "image '$E2E_IMAGE' not found or does not run; run: make test-e2e-image"
ok "image $E2E_IMAGE"

# 3. TUN kernel module available (create device node inside container if needed).
docker run --rm --cap-add NET_ADMIN --cap-add MKNOD "$E2E_IMAGE" \
  sh -c 'mkdir -p /dev/net; [ -c /dev/net/tun ] || mknod -m 0666 /dev/net/tun c 10 200' \
  >/dev/null 2>&1 ||
  fail "cannot create /dev/net/tun inside container (TUN kernel module required)"
ok "/dev/net/tun"

# 4. NET_ADMIN capability works inside a container.
docker run --rm --cap-add NET_ADMIN "$E2E_IMAGE" ip link show >/dev/null 2>&1 ||
  fail "NET_ADMIN not granted (check Docker daemon / security policy)"
ok "NET_ADMIN"

# 5. Can create a WireGuard interface inside a container.
docker run --rm \
  --cap-add NET_ADMIN \
  "$E2E_IMAGE" \
  sh -c 'ip link add wg-test type wireguard && ip link del wg-test' \
  >/dev/null 2>&1 ||
  fail "cannot create WireGuard interface inside container"
ok "WireGuard interface creation"

# 6. wg2awg binary runs and reports a version.
docker run --rm "$E2E_IMAGE" wg2awg --version >/dev/null 2>&1 ||
  fail "wg2awg --version failed"
ok "wg2awg --version"

echo "preflight: all checks passed"
