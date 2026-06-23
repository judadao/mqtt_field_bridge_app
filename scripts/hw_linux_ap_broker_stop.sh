#!/usr/bin/env bash
# Stop the Linux broker started by hw_linux_ap_broker_bridge_test.sh.
# Set STOP_AP=1 to also bring down the NetworkManager hotspot connection.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
STOP_AP=${STOP_AP:-0}
HOTSPOT_CONN=${HOTSPOT_CONN:-Hotspot-1}

PID_FILE="$LOG_DIR/linux-broker.pid"
if [ -f "$PID_FILE" ]; then
    PID=$(cat "$PID_FILE")
    if [ -n "$PID" ] && kill -0 "$PID" >/dev/null 2>&1; then
        kill "$PID"
        wait "$PID" >/dev/null 2>&1 || true
        printf 'Stopped Linux broker PID %s\n' "$PID"
    else
        printf 'Linux broker PID from %s is not running\n' "$PID_FILE"
    fi
else
    printf 'No broker PID file found: %s\n' "$PID_FILE"
fi

if [ "$STOP_AP" = "1" ]; then
    nmcli connection down "$HOTSPOT_CONN" >/dev/null 2>&1 || true
    printf 'Stopped hotspot connection: %s\n' "$HOTSPOT_CONN"
fi
