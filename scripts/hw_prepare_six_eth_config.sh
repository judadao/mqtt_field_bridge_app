#!/usr/bin/env bash
# Flash six ESP32 W5500 targets and provision static Ethernet broker settings.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WEST_WORKDIR=${WEST_WORKDIR:-"$ROOT_DIR/deps/dephy/zephyrproject"}
if [ ! -x "$WEST_WORKDIR/.venv/bin/west" ]; then
    WEST_WORKDIR=${WEST_WORKDIR_FALLBACK:-"/home/judd/moxa/personal/dephy/zephyrproject"}
fi
WEST=${WEST:-"$WEST_WORKDIR/.venv/bin/west"}
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build_product_eth"}
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware/six-eth-config-$(date +%Y%m%d-%H%M%S)"}
BAUD=${BAUD:-115200}
FLASH_RETRIES=${FLASH_RETRIES:-3}
PORT_WAIT_SECONDS=${PORT_WAIT_SECONDS:-20}
CONFIG_ERASE_OFFSET=${CONFIG_ERASE_OFFSET:-0x3b0000}
CONFIG_ERASE_SIZE=${CONFIG_ERASE_SIZE:-0x30000}

PATH="$WEST_WORKDIR/.venv/bin:$PATH"
export PATH

ports=(/dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2 /dev/ttyUSB3 /dev/ttyUSB4 /dev/ttyUSB5)
mgmt_ips=(192.168.127.4 192.168.127.5 192.168.127.6 192.168.127.7 192.168.127.8 192.168.127.9)
broker_ips=(192.168.127.15 192.168.127.16 192.168.127.17 192.168.127.18 192.168.127.19 192.168.127.20)

mkdir -p "$LOG_DIR"

summary() {
    printf '%s\n' "$*" | tee -a "$LOG_DIR/summary.log"
}

wait_for_port() {
    port=$1
    deadline=$((SECONDS + PORT_WAIT_SECONDS))
    while [ "$SECONDS" -le "$deadline" ]; do
        if [ -c "$port" ]; then
            return 0
        fi
        sleep 1
    done
    summary "FAIL $port did not appear within ${PORT_WAIT_SECONDS}s"
    return 1
}

flash_port() {
    port=$1
    idx=$2
    attempt=1
    while [ "$attempt" -le "$FLASH_RETRIES" ]; do
        wait_for_port "$port"
        summary "flash $port attempt $attempt/$FLASH_RETRIES"
        if (cd "$WEST_WORKDIR" &&
            "$WEST" flash -d "$BUILD_DIR" --runner esp32 --esp-device "$port") \
            >"$LOG_DIR/flash-ttyUSB${idx}-attempt${attempt}.log" 2>&1; then
            return 0
        fi
        sleep 3
        attempt=$((attempt + 1))
    done
    summary "FAIL $port flash failed after $FLASH_RETRIES attempts"
    return 1
}

erase_config() {
    port=$1
    idx=$2
    wait_for_port "$port"
    esptool --port "$port" erase-region "$CONFIG_ERASE_OFFSET" "$CONFIG_ERASE_SIZE" \
        >"$LOG_DIR/erase-ttyUSB${idx}.log" 2>&1
}

provision_config() {
    port=$1
    idx=$2
    mgmt_ip=$3
    broker_ip=$4
    wait_for_port "$port"
    python3 - "$port" "$BAUD" "$mgmt_ip" "$broker_ip" "$LOG_DIR/config-ttyUSB${idx}.log" <<'PY'
import serial
import sys
import time

port = sys.argv[1]
baud = int(sys.argv[2])
mgmt_ip = sys.argv[3]
broker_ip = sys.argv[4]
log_path = sys.argv[5]

commands = [
    f"ip {mgmt_ip} 192.168.127.1 255.255.255.0",
    f"broker-ip {broker_ip}",
    "broker-port 1883",
    "bridge-port 4884",
    "broker-state 1",
    "show",
    "reboot",
]

with serial.Serial(port, baudrate=baud, timeout=0.25) as ser, open(log_path, "wb") as out:
    ser.dtr = False
    ser.rts = False
    time.sleep(2.5)
    ser.reset_input_buffer()
    for cmd in commands:
        line = (cmd + "\n").encode()
        out.write(b"\n>>> " + line)
        out.flush()
        ser.write(line)
        ser.flush()
        deadline = time.monotonic() + 1.8
        while time.monotonic() < deadline:
            chunk = ser.read(4096)
            if not chunk:
                continue
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            out.write(chunk)
            out.flush()
PY
}

summary "six-node Ethernet config preparation started"
summary "log_dir=$LOG_DIR"
summary "build_dir=$BUILD_DIR"

for idx in "${!ports[@]}"; do
    port=${ports[$idx]}
    mgmt_ip=${mgmt_ips[$idx]}
    broker_ip=${broker_ips[$idx]}
    summary "START node$((idx + 1)) port=$port mgmt=$mgmt_ip broker=$broker_ip mqtt=1883 bridge=4884"
    flash_port "$port" "$idx"
    erase_config "$port" "$idx"
    provision_config "$port" "$idx" "$mgmt_ip" "$broker_ip"
    summary "OK node$((idx + 1)) port=$port mgmt=$mgmt_ip broker=$broker_ip"
done

summary "DONE six-node Ethernet config preparation complete"
