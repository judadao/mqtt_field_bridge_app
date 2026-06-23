#!/usr/bin/env bash
# Build, flash, and capture UART logs from the ESP32 product app.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
BAUD=${BAUD:-115200}
LOG_SECONDS=${LOG_SECONDS:-45}
SKIP_BUILD=${SKIP_BUILD:-0}
SKIP_FLASH=${SKIP_FLASH:-0}
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build_product"}

mkdir -p "$LOG_DIR"

find_serial_port() {
    if [ -n "${SERIAL_PORT:-}" ]; then
        printf '%s\n' "$SERIAL_PORT"
        return 0
    fi

    if [ -d /dev/serial/by-id ]; then
        for dev in /dev/serial/by-id/*CP210* /dev/serial/by-id/*CH340* /dev/serial/by-id/*UART*; do
            [ -e "$dev" ] || continue
            readlink -f "$dev"
            return 0
        done
    fi

    for dev in /dev/ttyUSB*; do
        [ -e "$dev" ] || continue
        printf '%s\n' "$dev"
        return 0
    done

    printf 'error: no ESP32 serial port found; refusing to use /dev/ttyACM0\n' >&2
    return 1
}

find_west_workspace() {
    if [ -x "$ROOT_DIR/deps/dephy/zephyrproject/.venv/bin/west" ]; then
        printf '%s\n%s\n' "$ROOT_DIR/deps/dephy/zephyrproject/.venv/bin/west" \
            "$ROOT_DIR/deps/dephy/zephyrproject"
    elif [ -x "$ROOT_DIR/../dephy/zephyrproject/.venv/bin/west" ]; then
        printf '%s\n%s\n' "$ROOT_DIR/../dephy/zephyrproject/.venv/bin/west" \
            "$ROOT_DIR/../dephy/zephyrproject"
    elif command -v west >/dev/null 2>&1; then
        printf '%s\n%s\n' "$(command -v west)" "$ROOT_DIR"
    else
        printf 'error: west not found; run ./scripts/sync_deps.sh init first\n' >&2
        return 1
    fi
}

PORT=$(find_serial_port)
WEST_INFO=$(find_west_workspace)
WEST=$(printf '%s\n' "$WEST_INFO" | sed -n '1p')
WEST_WORKDIR=$(printf '%s\n' "$WEST_INFO" | sed -n '2p')
if [ -d "$WEST_WORKDIR/.venv/bin" ]; then
    PATH="$WEST_WORKDIR/.venv/bin:$PATH"
    export PATH
fi
STAMP=$(date +%Y%m%d-%H%M%S)
FLASH_LOG="$LOG_DIR/flash-$STAMP.log"
UART_LOG="$LOG_DIR/uart-$STAMP.log"

printf 'ESP32 serial: %s\n' "$PORT"
printf 'Flash log:    %s\n' "$FLASH_LOG"
printf 'UART log:     %s\n' "$UART_LOG"

if [ "$SKIP_BUILD" != 1 ]; then
    (cd "$ROOT_DIR" && ./scripts/build_product.sh) 2>&1 | tee "$LOG_DIR/build-$STAMP.log"
fi

if [ "$SKIP_FLASH" != 1 ]; then
    (cd "$WEST_WORKDIR" && "$WEST" flash -d "$BUILD_DIR" --runner esp32 --esp-device "$PORT") \
        2>&1 | tee "$FLASH_LOG"
fi

python3 - "$PORT" "$BAUD" "$LOG_SECONDS" "$UART_LOG" <<'PY'
import serial
import sys
import time

port = sys.argv[1]
baud = int(sys.argv[2])
seconds = float(sys.argv[3])
log_path = sys.argv[4]
deadline = time.monotonic() + seconds

with serial.Serial(port, baudrate=baud, timeout=0.25) as ser, open(log_path, "wb") as out:
    ser.dtr = False
    ser.rts = False
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        out.write(chunk)
        out.flush()
PY

printf '\nCaptured UART log: %s\n' "$UART_LOG"
