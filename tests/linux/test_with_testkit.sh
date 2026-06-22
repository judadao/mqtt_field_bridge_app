#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TESTKIT="${DEPHY_TESTKIT_ROOT:-$ROOT_DIR/deps/dephy_testkit}"

if [ ! -x "$TESTKIT/scripts/run_with_result.sh" ]; then
    TESTKIT="$ROOT_DIR/../dephy_testkit"
fi
if [ ! -x "$TESTKIT/scripts/run_with_result.sh" ]; then
    echo "missing dephy_testkit" >&2
    exit 1
fi

"$TESTKIT/scripts/run_with_result.sh" mqtt_field_bridge_unit \
    make -C "$ROOT_DIR/tests/linux" unit-tests
