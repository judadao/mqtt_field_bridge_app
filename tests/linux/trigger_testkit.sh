#!/usr/bin/env sh
# Trigger a product test case through dephy_testkit result wrapping.
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: $0 CASE_NAME COMMAND [ARG ...]" >&2
    exit 2
fi

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TESTKIT="${DEPHY_TESTKIT_ROOT:-$ROOT_DIR/deps/dephy_testkit}"

if [ ! -x "$TESTKIT/scripts/run_with_result.sh" ]; then
    TESTKIT="$ROOT_DIR/../dephy_testkit"
fi
if [ ! -x "$TESTKIT/scripts/run_with_result.sh" ]; then
    echo "missing dephy_testkit; run ./scripts/sync_deps.sh download or set DEPHY_TESTKIT_ROOT" >&2
    exit 1
fi

"$TESTKIT/scripts/run_with_result.sh" "$@"
