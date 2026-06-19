#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BOARD=$(sed -n 's/.*"board": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)

if [ -z "$BOARD" ]; then
    BOARD=esp32
fi

west build -b "$BOARD" "$ROOT_DIR/app" \
    -- -DZEPHYR_EXTRA_MODULES="$ROOT_DIR/deps/mqtt_min_broker"
