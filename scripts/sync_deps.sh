#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# ---------------------------------------------------------------------------
# JSON parsing: prefer jq, fall back to sed
# ---------------------------------------------------------------------------
if command -v jq >/dev/null 2>&1; then
    BROKER_REPO=$(   jq -r '.deps.mqtt_min_broker.repo'    "$ROOT_DIR/deps.json")
    BROKER_VERSION=$(jq -r '.deps.mqtt_min_broker.version' "$ROOT_DIR/deps.json")
    BROKER_PATH=$(   jq -r '.deps.mqtt_min_broker.path'    "$ROOT_DIR/deps.json")
else
    BROKER_REPO=$(   sed -n 's/.*"repo": *"\([^"]*\)".*/\1/p'    "$ROOT_DIR/deps.json" | head -n 1)
    BROKER_VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
    BROKER_PATH=$(   sed -n 's/.*"path": *"\([^"]*\)".*/\1/p'    "$ROOT_DIR/deps.json" | head -n 1)
fi

# ---------------------------------------------------------------------------
# --version: print pinned broker tag and exit
# ---------------------------------------------------------------------------
if [ "${1:-}" = "--version" ]; then
    printf '%s\n' "$BROKER_VERSION"
    exit 0
fi

if [ -z "$BROKER_REPO" ] || [ -z "$BROKER_VERSION" ] || [ -z "$BROKER_PATH" ]; then
    printf 'error: failed to parse deps.json\n' >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Resolve repo path to absolute (handles ../relative, /absolute, and URLs)
# ---------------------------------------------------------------------------
case "$BROKER_REPO" in
    /*|git@*|https://*)
        BROKER_REPO_ABS="$BROKER_REPO" ;;
    *)
        BROKER_REPO_ABS=$(realpath -m "$ROOT_DIR/$BROKER_REPO") ;;
esac

# ---------------------------------------------------------------------------
# Validate the pinned tag exists in the source repo before doing any work
# ---------------------------------------------------------------------------
if ! git ls-remote --tags "$BROKER_REPO_ABS" "refs/tags/$BROKER_VERSION" 2>/dev/null | grep -q .; then
    printf 'error: tag %s not found in %s\n' "$BROKER_VERSION" "$BROKER_REPO_ABS" >&2
    printf '  Create it: git -C %s tag -a %s -m %s && git -C %s push origin %s\n' \
        "$BROKER_REPO_ABS" "$BROKER_VERSION" "$BROKER_VERSION" \
        "$BROKER_REPO_ABS" "$BROKER_VERSION" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Clone if not yet present
# ---------------------------------------------------------------------------
mkdir -p "$ROOT_DIR/deps"

if [ ! -d "$ROOT_DIR/$BROKER_PATH/.git" ]; then
    git clone "$BROKER_REPO_ABS" "$ROOT_DIR/$BROKER_PATH"
fi

# ---------------------------------------------------------------------------
# Dirty check: refuse to overwrite local modifications
# ---------------------------------------------------------------------------
if [ -n "$(git -C "$ROOT_DIR/$BROKER_PATH" status --porcelain)" ]; then
    printf 'error: %s has local modifications\n' "$BROKER_PATH" >&2
    printf '  Commit, stash, or discard changes before syncing.\n' >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Fetch latest tags and check out the pinned version
# ---------------------------------------------------------------------------
git -C "$ROOT_DIR/$BROKER_PATH" fetch --tags --force
git -C "$ROOT_DIR/$BROKER_PATH" checkout "$BROKER_VERSION"

printf 'mqtt_min_broker synced to %s\n' "$BROKER_VERSION"
