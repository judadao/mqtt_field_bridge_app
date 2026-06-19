#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BROKER_REPO=$(sed -n 's/.*"repo": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
BROKER_VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
BROKER_PATH=$(sed -n 's/.*"path": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)

if [ -z "$BROKER_REPO" ] || [ -z "$BROKER_VERSION" ] || [ -z "$BROKER_PATH" ]; then
    echo "failed to parse deps.json" >&2
    exit 1
fi

mkdir -p "$ROOT_DIR/deps"

if [ ! -d "$ROOT_DIR/$BROKER_PATH/.git" ]; then
    git clone "$BROKER_REPO" "$ROOT_DIR/$BROKER_PATH"
fi

git -C "$ROOT_DIR/$BROKER_PATH" fetch --tags
git -C "$ROOT_DIR/$BROKER_PATH" checkout "$BROKER_VERSION"

echo "mqtt_min_broker synced to $BROKER_VERSION"
