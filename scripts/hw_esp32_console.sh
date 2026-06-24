#!/usr/bin/env bash
# Send commands to the ESP32 UART console. Refuses to use /dev/ttyACM0.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SERIAL_PORT=${SERIAL_PORT:-}
BAUD=${BAUD:-115200}
READ_SECONDS=${READ_SECONDS:-3}

find_serial_port() {
    if [ -n "$SERIAL_PORT" ]; then
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

PORT=$(find_serial_port)
COMMAND=${*:-}
if [ "${READ_SECONDS}" = "3" ] && [ "$COMMAND" = "scan" ]; then
    READ_SECONDS=14
fi
stty -F "$PORT" "$BAUD" raw -echo -hupcl >/dev/null 2>&1 || true

python3 - "$PORT" "$BAUD" "$READ_SECONDS" "$COMMAND" <<'PY'
import select
import serial
from serial.serialutil import SerialException
import sys
import time

port = sys.argv[1]
baud = int(sys.argv[2])
read_seconds = float(sys.argv[3])
command = sys.argv[4]

def pump_serial(ser, deadline, capture=None):
    while time.monotonic() < deadline:
        try:
            chunk = ser.read(4096)
        except SerialException:
            time.sleep(0.05)
            continue
        if chunk:
            if capture is not None:
                capture.append(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()

ser = serial.Serial()
ser.port = port
ser.baudrate = baud
ser.timeout = 0.1
ser.dtr = False
ser.rts = False
ser.open()
try:
    ser.dtr = False
    ser.rts = False
    if command:
        seen = []
        ser.reset_input_buffer()
        pump_serial(ser, time.monotonic() + 2.0, seen)
        if b"field-bridge console ready" not in b"".join(seen):
            pump_serial(ser, time.monotonic() + 4.0, seen)
        ser.write((command + "\n").encode("utf-8"))
        ser.flush()
        pump_serial(ser, time.monotonic() + read_seconds)
    else:
        print(f"ESP32 console on {port} @ {baud}. Type commands; Ctrl-C exits.")
        print("Try: help, status, show, scan, wifi <ssid> <pass>, clear-wifi, reboot")
        while True:
            pump_serial(ser, time.monotonic() + 0.05)
            readable, _, _ = select.select([sys.stdin], [], [], 0.05)
            if readable:
                line = sys.stdin.readline()
                if not line:
                    break
                ser.write(line.encode("utf-8"))
                ser.flush()
finally:
    ser.close()
PY
