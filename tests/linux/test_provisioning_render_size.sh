#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HDR="$ROOT_DIR/app/src/generated/provisioning_index.h"

make -C "$ROOT_DIR/tests/linux" "$HDR" >/dev/null
bytes=$(wc -c < "$HDR" | tr -d ' ')
limit=${PROVISIONING_HTML_HEADER_LIMIT:-65536}

printf 'provisioning_header_bytes=%s limit=%s\n' "$bytes" "$limit"
if [ "$bytes" -gt "$limit" ]; then
    echo "provisioning generated header exceeds limit" >&2
    exit 1
fi
