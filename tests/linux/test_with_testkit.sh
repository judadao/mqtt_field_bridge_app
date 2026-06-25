#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

"$ROOT_DIR/tests/linux/trigger_testkit.sh" mqtt_field_bridge_unit \
    make -C "$ROOT_DIR/tests/linux" unit-tests
