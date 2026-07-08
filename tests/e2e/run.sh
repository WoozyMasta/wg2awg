#!/usr/bin/env bash
# Main e2e test runner for wg2awg.
# Usage:
#   bash tests/e2e/run.sh               # run all tests
#   E2E_FAST=1 bash tests/e2e/run.sh    # preflight + test_01 + test_02 only
#   E2E_FAIL_FAST=1 bash tests/e2e/run.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

TESTS=(
  test_00_preflight
  test_01_plain_wg
  test_02_client_gw
  test_03_awg_server
  test_04_awg_client
  test_05_knobs
  test_06_server
  test_07_edge
)

if [ "${E2E_FAST:-0}" = "1" ]; then
  TESTS=(test_00_preflight test_01_plain_wg test_02_client_gw)
fi

# Remove any stale containers from previous interrupted runs.
docker ps -aq --filter "name=e2e-0" | xargs -r docker rm -f >/dev/null 2>&1 || true

PASS=0
FAIL=0

for t in "${TESTS[@]}"; do
  echo ""
  echo "=== $t ==="
  if bash "$SCRIPT_DIR/${t}.sh"; then
    echo "PASS: $t"
    PASS=$((PASS + 1))
  else
    echo "FAIL: $t"
    FAIL=$((FAIL + 1))
    if [ "${E2E_FAIL_FAST:-0}" = "1" ]; then
      break
    fi
  fi
done

echo ""
echo "Results: $PASS pass, $FAIL fail"
[ "$FAIL" -eq 0 ]
