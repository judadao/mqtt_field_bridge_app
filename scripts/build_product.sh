#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if command -v jq >/dev/null 2>&1; then
    BOARD=$(jq -r '.build.board // empty' "$ROOT_DIR/deps.json")
    EXTRA_MODULES=$(jq -r '.deps | to_entries | map(.value.module_path // .value.path) | join(";")' "$ROOT_DIR/deps.json")
    DEPHY_PATH=$(jq -r '.deps.dephy.path // empty' "$ROOT_DIR/deps.json")
else
    BOARD=$(sed -n 's/.*"board": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
    EXTRA_MODULES=$(sed -n 's/.*"path": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | paste -sd ';' -)
    DEPHY_PATH=$(sed -n '/"dephy"/,/"}/s/.*"path": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
fi

if [ -z "$BOARD" ]; then
    BOARD=esp32
fi

if [ -z "$EXTRA_MODULES" ]; then
    printf 'error: failed to parse dependency module paths from deps.json\n' >&2
    exit 1
fi

EXTRA_MODULES_ABS=""
OLD_IFS=$IFS
IFS=';'
for module_path in $EXTRA_MODULES; do
    if [ -z "$EXTRA_MODULES_ABS" ]; then
        EXTRA_MODULES_ABS="$ROOT_DIR/$module_path"
    else
        EXTRA_MODULES_ABS="$EXTRA_MODULES_ABS;$ROOT_DIR/$module_path"
    fi
done
IFS=$OLD_IFS

if command -v west >/dev/null 2>&1; then
    WEST=$(command -v west)
    WEST_WORKDIR=$ROOT_DIR
elif [ -n "$DEPHY_PATH" ] && [ -x "$ROOT_DIR/$DEPHY_PATH/zephyrproject/.venv/bin/west" ]; then
    WEST="$ROOT_DIR/$DEPHY_PATH/zephyrproject/.venv/bin/west"
    WEST_WORKDIR="$ROOT_DIR/$DEPHY_PATH/zephyrproject"
else
    printf 'error: west not found; run scripts/sync_deps.sh init first\n' >&2
    exit 1
fi

(cd "$WEST_WORKDIR" && "$WEST" build -b "$BOARD" "$ROOT_DIR/app" \
    -- -DZEPHYR_EXTRA_MODULES="$EXTRA_MODULES_ABS")
