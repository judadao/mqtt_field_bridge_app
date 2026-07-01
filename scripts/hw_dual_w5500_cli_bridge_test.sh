#!/usr/bin/env bash
# Flash two W5500 ESP32 boards, provision bridge peers through UART CLI, and
# verify bidirectional MQTT delivery.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WEST_WORKDIR=${WEST_WORKDIR:-"$ROOT_DIR/deps/dephy/zephyrproject"}
if [ ! -x "$WEST_WORKDIR/.venv/bin/west" ]; then
    WEST_WORKDIR=${WEST_WORKDIR_FALLBACK:-"/home/judd/moxa/personal/dephy/zephyrproject"}
fi
PATH="$WEST_WORKDIR/.venv/bin:$PATH"
export PATH

WEST=${WEST:-"$WEST_WORKDIR/.venv/bin/west"}
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build_product_eth"}
BROKER_DIR=${BROKER_DIR:-"$ROOT_DIR/deps/mqtt_min_broker"}
CLI=${CLI:-"$BROKER_DIR/build_out/mqtt_cli"}
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/tests/linux/out/dual_w5500_cli_bridge-$(date +%Y%m%d-%H%M%S)"}
BAUD=${BAUD:-115200}
MQTT_PORT=${MQTT_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}
CONFIG_ERASE_OFFSET=${CONFIG_ERASE_OFFSET:-0x3b0000}
CONFIG_ERASE_SIZE=${CONFIG_ERASE_SIZE:-0x30000}
PORT_WAIT_SECONDS=${PORT_WAIT_SECONDS:-25}
SETTLE_SEC=${SETTLE_SEC:-18}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-45}
SUB_PROP_SEC=${SUB_PROP_SEC:-8}
IFACE=${IFACE:-}

ESP_A_PORT=${ESP_A_PORT:-/dev/ttyUSB0}
ESP_A_HTTP=${ESP_A_HTTP:-192.168.127.4}
ESP_A_BROKER=${ESP_A_BROKER:-192.168.127.15}
ESP_B_PORT=${ESP_B_PORT:-/dev/ttyUSB1}
ESP_B_HTTP=${ESP_B_HTTP:-192.168.127.6}
ESP_B_BROKER=${ESP_B_BROKER:-192.168.127.16}
GATEWAY=${GATEWAY:-192.168.127.1}
NETMASK=${NETMASK:-255.255.255.0}
TOPIC=${TOPIC:-site/field-a/dual-w5500-cli}

mkdir -p "$OUT_DIR"

log() {
    printf '%s\n' "$*" | tee -a "$OUT_DIR/summary.log"
}

wait_for_port() {
    port=$1
    deadline=$((SECONDS + PORT_WAIT_SECONDS))
    while [ "$SECONDS" -le "$deadline" ]; do
        [ -c "$port" ] && return 0
        sleep 1
    done
    log "FAIL missing serial port $port"
    return 1
}

flash_and_erase() {
    label=$1
    port=$2
    wait_for_port "$port"
    log "flash $label $port"
    (cd "$WEST_WORKDIR" &&
        "$WEST" flash -d "$BUILD_DIR" --runner esp32 --esp-device "$port") \
        >"$OUT_DIR/flash-$label.log" 2>&1
    wait_for_port "$port"
    log "erase config $label $port"
    esptool --port "$port" erase-region "$CONFIG_ERASE_OFFSET" "$CONFIG_ERASE_SIZE" \
        >"$OUT_DIR/erase-$label.log" 2>&1
}

provision_uart() {
    label=$1
    port=$2
    mgmt_ip=$3
    broker_ip=$4
    peer_host=$5
    peer_enabled=$6

    wait_for_port "$port"
    log "provision $label $port mgmt=$mgmt_ip broker=$broker_ip peer=$peer_host enabled=$peer_enabled"
    python3 - "$port" "$BAUD" "$mgmt_ip" "$broker_ip" "$peer_host" "$peer_enabled" \
        "$MQTT_PORT" "$P2P_PORT" "$GATEWAY" "$NETMASK" "$OUT_DIR/uart-$label.log" \
        2>>"$OUT_DIR/uart-$label.log" <<'PY'
import serial
import sys
import time

port = sys.argv[1]
baud = int(sys.argv[2])
mgmt_ip = sys.argv[3]
broker_ip = sys.argv[4]
peer_host = sys.argv[5]
peer_enabled = sys.argv[6]
mqtt_port = sys.argv[7]
p2p_port = sys.argv[8]
gateway = sys.argv[9]
netmask = sys.argv[10]
log_path = sys.argv[11]

commands = [
    (f"ip {mgmt_ip} {gateway} {netmask}", ["OK saved static"]),
    (f"broker-ip {broker_ip}", ["OK saved broker-ip"]),
    (f"broker-port {mqtt_port}", ["OK saved broker-port"]),
    (f"bridge-port {p2p_port}", ["OK saved bridge-port"]),
    ("broker-state 1", ["OK saved broker-state"]),
    (f"peer 0 peer {peer_host} {mqtt_port} {p2p_port} {peer_enabled}", ["OK saved peer 0"]),
    ("show", ["Saved config"]),
    ("reboot", ["OK rebooting", "rst:", "P2P static seed-only broker mode enabled"]),
]

def read_until(ser, out, needles, seconds):
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if chunk:
            data.extend(chunk)
            out.write(chunk)
            out.flush()
            text = data.decode("utf-8", errors="replace")
            if "ZEPHYR FATAL ERROR" in text or "Halting system" in text:
                raise RuntimeError("device fatal error during UART provisioning")
            if any(needle in text for needle in needles):
                return text
        else:
            time.sleep(0.05)
    text = data.decode("utf-8", errors="replace")
    raise TimeoutError(f"timed out waiting for {needles}; saw: {text[-300:]}")

with serial.Serial(port, baudrate=baud, timeout=0.2) as ser, open(log_path, "wb") as out:
    ser.dtr = False
    ser.rts = False
    time.sleep(0.5)
    ser.reset_input_buffer()
    # The app can already be past the banner by the time serial opens; menu is a
    # harmless readiness probe that always prints a page when the CLI is alive.
    ser.write(b"menu\n")
    ser.flush()
    read_until(ser, out, ["Field Bridge CLI menu", "commands:"], 10)
    for cmd, expected in commands:
        line = (cmd + "\n").encode()
        out.write(b"\n>>> " + line)
        out.flush()
        ser.write(line)
        ser.flush()
        read_until(ser, out, expected, 12)
PY
}

wait_for_mqtt() {
    host=$1
    port=$2
    seconds=$3
    deadline=$((SECONDS + seconds))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if "$CLI" status -h "$host" -p "$port" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

detect_iface() {
    if [ -n "$IFACE" ]; then
        printf '%s\n' "$IFACE"
        return 0
    fi
    ip route get "$ESP_A_HTTP" | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n 1
}

wait_for_match() {
    file=$1
    pattern=$2
    seconds=$3
    deadline=$((SECONDS + seconds))
    while [ "$SECONDS" -lt "$deadline" ]; do
        rg -q --fixed-strings "$pattern" "$file" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

publish_until_match() {
    host=$1
    topic=$2
    payload=$3
    file=$4
    deadline=$((SECONDS + WAIT_MSG_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$CLI" pub -h "$host" -p "$MQTT_PORT" -t "$topic" -m "$payload" >/dev/null 2>&1 || true
        wait_for_match "$file" "$payload" 1 && return 0
        sleep 0.5
    done
    return 1
}

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

log "dual W5500 CLI bridge test started"
log "out_dir=$OUT_DIR"
log "ESP A port=$ESP_A_PORT mgmt=$ESP_A_HTTP broker=$ESP_A_BROKER"
log "ESP B port=$ESP_B_PORT mgmt=$ESP_B_HTTP broker=$ESP_B_BROKER"

flash_and_erase "esp-a" "$ESP_A_PORT"
provision_uart "esp-a" "$ESP_A_PORT" "$ESP_A_HTTP" "$ESP_A_BROKER" "$ESP_B_BROKER" 1
flash_and_erase "esp-b" "$ESP_B_PORT"
provision_uart "esp-b" "$ESP_B_PORT" "$ESP_B_HTTP" "$ESP_B_BROKER" "$ESP_A_BROKER" 0

log "waiting for reboot/network settle ${SETTLE_SEC}s"
sleep "$SETTLE_SEC"

IFACE=$(detect_iface)
if [ -z "$IFACE" ]; then
    log "FAIL could not detect network interface for $ESP_A_HTTP"
    exit 1
fi
log "iface=$IFACE"

wait_for_mqtt "$ESP_A_BROKER" "$MQTT_PORT" "$WAIT_MSG_SEC" ||
    { log "FAIL ESP A broker MQTT unreachable"; exit 1; }
log "PASS ESP A broker MQTT reachable"
wait_for_mqtt "$ESP_B_BROKER" "$MQTT_PORT" "$WAIT_MSG_SEC" ||
    { log "FAIL ESP B broker MQTT unreachable"; exit 1; }
log "PASS ESP B broker MQTT reachable"

recv_ab="$OUT_DIR/recv-b.log"
recv_ba="$OUT_DIR/recv-a.log"
rm -f "$recv_ab" "$recv_ba"

topic_ab="$TOPIC/a-to-b"
msg_ab="esp-a-to-esp-b-$(date +%s)"
"$CLI" sub -h "$ESP_B_BROKER" -p "$MQTT_PORT" -t "$topic_ab" >"$recv_ab" 2>&1 &
sub_pid=$!
sleep "$SUB_PROP_SEC"
if publish_until_match "$ESP_A_BROKER" "$topic_ab" "$msg_ab" "$recv_ab"; then
    log "PASS ESP B receives publish sent to ESP A broker"
else
    kill "$sub_pid" >/dev/null 2>&1 || true
    wait "$sub_pid" >/dev/null 2>&1 || true
    log "FAIL ESP B did not receive ESP A publish"
    exit 1
fi
kill "$sub_pid" >/dev/null 2>&1 || true
wait "$sub_pid" >/dev/null 2>&1 || true

topic_ba="$TOPIC/b-to-a"
msg_ba="esp-b-to-esp-a-$(date +%s)"
"$CLI" sub -h "$ESP_A_BROKER" -p "$MQTT_PORT" -t "$topic_ba" >"$recv_ba" 2>&1 &
sub_pid=$!
sleep "$SUB_PROP_SEC"
if publish_until_match "$ESP_B_BROKER" "$topic_ba" "$msg_ba" "$recv_ba"; then
    log "PASS ESP A receives publish sent to ESP B broker"
else
    kill "$sub_pid" >/dev/null 2>&1 || true
    wait "$sub_pid" >/dev/null 2>&1 || true
    log "FAIL ESP A did not receive ESP B publish"
    exit 1
fi
kill "$sub_pid" >/dev/null 2>&1 || true
wait "$sub_pid" >/dev/null 2>&1 || true

log "DONE dual W5500 CLI bridge test passed"
