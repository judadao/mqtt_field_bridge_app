#!/usr/bin/env bash
set -u

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${OUT:-"$ROOT_DIR/tests/linux/out/hardware/dual-w5500-netcheck.log"}

{
    date
    ip route get 192.168.127.15 || true
    ip neigh show | grep -E '192\.168\.127\.(4|6|15|16)' || true
    if timeout 3 bash -c '</dev/tcp/192.168.127.15/1883'; then
        echo A_OPEN
    else
        echo A_CLOSED
    fi
    if timeout 3 bash -c '</dev/tcp/192.168.127.16/1883'; then
        echo B_OPEN
    else
        echo B_CLOSED
    fi
} >"$OUT" 2>&1
