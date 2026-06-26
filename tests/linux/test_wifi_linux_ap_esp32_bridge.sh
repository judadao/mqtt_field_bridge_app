#!/usr/bin/env bash
# Validate ESP32 WiFi broker bridge nodes on one Linux-hosted AP.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/wifi_linux_ap_bridge"}
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build_wifi_bridge_product"}
NODE_COUNT=${NODE_COUNT:-2}
ESP_PORTS=${ESP_PORTS:-"/dev/ttyUSB2 /dev/ttyUSB3"}
NODE_IPS=${NODE_IPS:-}
WIFI_IFACE=${WIFI_IFACE:-}
LINUX_AP_SSID=${LINUX_AP_SSID:-Linux-Bridge-Test}
LINUX_AP_PASS=${LINUX_AP_PASS-bridge1234}
LINUX_AP_CHANNEL=${LINUX_AP_CHANNEL:-1}
LINUX_AP_CONN=${LINUX_AP_CONN:-Linux-Bridge-Test-esp32-bridge}
LINUX_AP_ADDR=${LINUX_AP_ADDR:-10.88.0.1/24}
MQTT_PORT=${MQTT_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}
WAIT_SECONDS=${WAIT_SECONDS:-180}
SETTLE_SECONDS=${SETTLE_SECONDS:-35}
SKIP_BUILD=${SKIP_BUILD:-0}
SKIP_FLASH=${SKIP_FLASH:-0}
ERASE_CONFIG=${ERASE_CONFIG:-1}
KEEP_AP=${KEEP_AP:-0}
SETUP_LINUX_AP=${SETUP_LINUX_AP:-1}
CONFIG_ERASE_OFFSET=${CONFIG_ERASE_OFFSET:-0x3b0000}
CONFIG_ERASE_SIZE=${CONFIG_ERASE_SIZE:-0x30000}
RUNTIME_SERIAL=${RUNTIME_SERIAL:-0}
EARLY_RUNTIME_SERIAL=${EARLY_RUNTIME_SERIAL:-0}
FLASH_ATTEMPTS=${FLASH_ATTEMPTS:-3}
PROVISION_BY_UART=${PROVISION_BY_UART:-1}
STATIC_NODE_IPS=${STATIC_NODE_IPS:-}
BROKER_ENABLED=${BROKER_ENABLED:-1}
HTTP_STABLE_REQUIRED=${HTTP_STABLE_REQUIRED:-2}
UART_PEER_CONFIG=${UART_PEER_CONFIG:-1}
LOAD_CLIENTS=${LOAD_CLIENTS:-}
LOAD_MIN_BROKERS=${LOAD_MIN_BROKERS:-}
LOAD_WAIT_SECONDS=${LOAD_WAIT_SECONDS:-45}
LOAD_TOPIC=${LOAD_TOPIC:-site/field-a/wifi-ap/load}

case "$NODE_COUNT" in
    2|3|4) ;;
    *)
        printf 'error: NODE_COUNT must be 2, 3, or 4\n' >&2
        exit 1
        ;;
esac
if [ -z "$LOAD_CLIENTS" ]; then
    LOAD_CLIENTS=$((NODE_COUNT * 2 - 1))
fi
if [ -z "$LOAD_MIN_BROKERS" ]; then
    LOAD_MIN_BROKERS=$NODE_COUNT
fi

mkdir -p "$LOG_DIR"
STAMP=$(date +%Y%m%d-%H%M%S)
RUN_LOG="$LOG_DIR/run-$STAMP.log"
exec > >(tee "$RUN_LOG") 2>&1
RUNTIME_PIDS=()
LOAD_PIDS=()
KEEPALIVE_PIDS=()

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'error: missing command: %s\n' "$1" >&2
        exit 1
    }
}

detect_wifi_iface() {
    if [ -n "$WIFI_IFACE" ]; then
        printf '%s\n' "$WIFI_IFACE"
        return 0
    fi
    nmcli -t -f DEVICE,TYPE device status |
        awk -F: '$2 == "wifi" { print $1; exit }'
}

cleanup() {
    if [ "$SETUP_LINUX_AP" = "1" ] && [ "$KEEP_AP" != "1" ]; then
        nmcli connection down "$LINUX_AP_CONN" >/dev/null 2>&1 || true
    fi
    for pid in "${RUNTIME_PIDS[@]:-}"; do
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
    if [ -n "${SUB_PID:-}" ]; then
        kill "$SUB_PID" >/dev/null 2>&1 || true
        wait "$SUB_PID" >/dev/null 2>&1 || true
    fi
    for pid in "${LOAD_PIDS[@]:-}"; do
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
    for pid in "${KEEPALIVE_PIDS[@]:-}"; do
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
}
trap cleanup EXIT INT TERM

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
        printf 'error: west not found; run ./scripts/sync_deps.sh replace/init first\n' >&2
        return 1
    fi
}

flash_node() {
    port=$1
    log_file=$2

    attempt=1
    while [ "$attempt" -le "$FLASH_ATTEMPTS" ]; do
        if (cd "$WEST_WORKDIR" && "$WEST" flash -d "$BUILD_DIR" --runner esp32 --esp-device "$port") \
            2>&1 | tee "$log_file"; then
            return 0
        fi
        if [ "$attempt" -lt "$FLASH_ATTEMPTS" ]; then
            printf 'warning: flash failed on %s; retrying (%s/%s)\n' \
                "$port" "$attempt" "$FLASH_ATTEMPTS" >&2
            sleep 2
        fi
        attempt=$((attempt + 1))
    done
    return 1
}

monitor_node_boot() {
    port=$1
    log=$2
    require_broker=${3:-1}
    stty -F "$port" 115200 raw -echo -hupcl >/dev/null 2>&1 || true
    python3 - "$port" "$log" "$require_broker" <<'PY'
import serial
import sys
import termios
import time

port = sys.argv[1]
log_path = sys.argv[2]
require_broker = sys.argv[3] == "1"
deadline = time.monotonic() + 75
seen_console = False
seen_broker = False
failed = False

with serial.Serial(port, baudrate=115200, timeout=0.25) as ser, open(log_path, "ab") as out:
    ser.dtr = False
    ser.rts = False
    ser.reset_input_buffer()
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            out.write(chunk)
            out.flush()
            if b"field-bridge console ready" in chunk:
                seen_console = True
            if b"mqtt_broker: Listening" in chunk or b"mqtt_broker: Listening on" in chunk:
                seen_broker = True
            if b"broker_init failed" in chunk or b"ZEPHYR FATAL ERROR" in chunk:
                if require_broker or b"ZEPHYR FATAL ERROR" in chunk:
                    failed = True
        if seen_console and (seen_broker or not require_broker):
            time.sleep(2.0)
            break

if failed:
    raise SystemExit("node reported broker/fatal failure")
if not seen_console:
    raise SystemExit("console readiness was not observed")
if require_broker and not seen_broker:
    raise SystemExit("broker readiness was not observed")
PY
}

console_command() {
    port=$1
    command=$2
    read_seconds=$3
    log_file=$4

    stty -F "$port" 115200 raw -echo -hupcl >/dev/null 2>&1 || true
    python3 - "$port" "$command" "$read_seconds" <<'PY' | tee "$log_file"
import serial
import sys
import termios
import time

port = sys.argv[1]
command = sys.argv[2]
read_seconds = float(sys.argv[3])

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.1
ser.dtr = False
ser.rts = False
ser.open()
try:
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()
    deadline = time.monotonic() + read_seconds
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
finally:
    ser.close()
PY
}

provision_node_uart() {
    port=$1
    static_ip=$2
    peer_commands=$3
    log_file=$4

    stty -F "$port" 115200 raw -echo -hupcl >/dev/null 2>&1 || true
    wifi_pass_arg=$LINUX_AP_PASS
    if [ -z "$wifi_pass_arg" ]; then
        wifi_pass_arg="-"
    fi

    python3 - "$port" "$static_ip" "${LINUX_AP_ADDR%/*}" "$LINUX_AP_SSID" "$wifi_pass_arg" "$BROKER_ENABLED" "$peer_commands" <<'PY' >"$log_file" 2>&1 &
import serial
import sys
import termios
import time

port = sys.argv[1]
static_ip = sys.argv[2]
gateway = sys.argv[3]
ssid = sys.argv[4]
password = sys.argv[5]
broker_enabled = sys.argv[6]
peer_commands = [cmd for cmd in sys.argv[7].split("|") if cmd]

def read_until(ser, needle, timeout):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if chunk:
            data.extend(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            if needle in data:
                return True
    return False

def read_boot_ready(ser, timeout):
    deadline = time.monotonic() + timeout
    data = bytearray()
    failed = False

    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if chunk:
            data.extend(chunk)
            if len(data) > 65536:
                del data[:-65536]
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            if b"broker_init failed" in data or b"ZEPHYR FATAL ERROR" in data:
                failed = True
        if failed:
            raise SystemExit("node reported broker/fatal failure")
        if broker_enabled == "1" and (
            b"mqtt_broker: Listening" in data or
            b"mqtt_broker: Listening on" in data
        ):
            time.sleep(2.0)
            return True
        if broker_enabled != "1" and (
            b"provisioning HTTP listening" in data and
            b"broker disabled by product config" in data
        ):
            time.sleep(2.0)
            return True
    return False

def send_cmd(ser, command, expect, timeout):
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()
    if not read_until(ser, expect, timeout):
        raise SystemExit("command failed or timed out: %s" % command)

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.1
ser.dtr = False
ser.rts = False
ser.open()
try:
    ser.dtr = False
    ser.rts = False
    attrs = termios.tcgetattr(ser.fileno())
    attrs[2] &= ~termios.HUPCL
    termios.tcsetattr(ser.fileno(), termios.TCSANOW, attrs)
    if not read_until(ser, b"field-bridge console ready", 75):
        raise SystemExit("console readiness was not observed")
    send_cmd(ser, "ip %s %s 255.255.255.0" % (static_ip, gateway),
             ("OK saved static ip=%s" % static_ip).encode("utf-8"), 8)
    send_cmd(ser, "wifi %s %s" % (ssid, password),
             b"OK saved wifi", 8)
    if broker_enabled != "1":
        send_cmd(ser, "broker-state 0",
                 b"OK saved broker-state=0", 8)
    for command in peer_commands:
        send_cmd(ser, command, b"OK saved peer", 8)
    ser.write(b"reboot\n")
    ser.flush()
    if not read_boot_ready(ser, 75):
        raise SystemExit("broker readiness was not observed after provisioning")
    while True:
        time.sleep(3600)
finally:
    ser.dtr = False
    ser.rts = False
    ser.close()
PY
    pid=$!
    RUNTIME_PIDS+=("$pid")

    deadline=$((SECONDS + 90))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if [ "$BROKER_ENABLED" = "1" ] && rg -q "mqtt_broker: Listening" "$log_file"; then
            tail -n 80 "$log_file"
            return 0
        fi
        if [ "$BROKER_ENABLED" != "1" ] &&
           rg -q "provisioning HTTP listening" "$log_file" &&
           rg -q "broker disabled by product config" "$log_file"; then
            tail -n 80 "$log_file"
            return 0
        fi
        if ! kill -0 "$pid" >/dev/null 2>&1; then
            cat "$log_file" >&2
            return 1
        fi
        sleep 1
    done
    cat "$log_file" >&2
    return 1
}

static_ip_for_node() {
    idx=$1
    gateway=${LINUX_AP_ADDR%/*}
    prefix=${gateway%.*}

    if [ -n "$STATIC_NODE_IPS" ]; then
        # shellcheck disable=SC2206
        ips=($STATIC_NODE_IPS)
        if [ -n "${ips[$idx]:-}" ]; then
            printf '%s\n' "${ips[$idx]}"
            return 0
        fi
    fi
    printf '%s.%s\n' "$prefix" "$((2 + idx))"
}

peer_commands_for_node() {
    idx=$1
    shift
    ips=("$@")
    count=${#ips[@]}

    if [ "$UART_PEER_CONFIG" != "1" ] || [ "$BROKER_ENABLED" != "1" ] ||
       [ "$count" -lt 2 ]; then
        return 0
    fi

    peer_idx=0
    if [ "$idx" -gt 0 ]; then
        prev=$((idx - 1))
        printf 'peer %s node%s %s %s %s 1\n' \
            "$peer_idx" "$prev" "${ips[$prev]}" "$MQTT_PORT" "$P2P_PORT"
        peer_idx=$((peer_idx + 1))
    fi
    if [ "$idx" -lt $((count - 1)) ]; then
        next=$((idx + 1))
        printf 'peer %s node%s %s %s %s 1\n' \
            "$peer_idx" "$next" "${ips[$next]}" "$MQTT_PORT" "$P2P_PORT"
    fi
}

expected_peer_count_for_node() {
    idx=$1
    count=$2

    if [ "$count" -le 1 ]; then
        printf '0\n'
    elif [ "$idx" -eq 0 ] || [ "$idx" -eq $((count - 1)) ]; then
        printf '1\n'
    else
        printf '2\n'
    fi
}

start_runtime_serial_log() {
    port=$1
    log=$2
    python3 - "$port" "$log" <<'PY' &
import serial
import sys
import time

port = sys.argv[1]
log_path = sys.argv[2]

while True:
    try:
        with serial.Serial(port, baudrate=115200, timeout=0.5) as ser, open(log_path, "ab") as out:
            ser.dtr = False
            ser.rts = False
            while True:
                chunk = ser.read(4096)
                if chunk:
                    out.write(chunk)
                    out.flush()
    except serial.SerialException as exc:
        with open(log_path, "ab") as out:
            out.write(("serial reopen after error: %s\n" % exc).encode("utf-8", "replace"))
            out.flush()
        time.sleep(1.0)
PY
    RUNTIME_PIDS+=("$!")
}

start_node_keepalive() {
    node=$1
    (
        while true; do
            ping -W 1 -c 1 "$node" >/dev/null 2>&1 || true
            curl -fsS --max-time 2 "http://$node:8080/status" >/dev/null 2>&1 || true
            sleep 2
        done
    ) &
    KEEPALIVE_PIDS+=("$!")
}

wait_for_nodes() {
    iface=$1
    count=$2
    out_file=$3
    gateway=${LINUX_AP_ADDR%/*}
    prefix=${gateway%.*}
    deadline=$((SECONDS + WAIT_SECONDS))
    : >"$out_file"
    while [ "$SECONDS" -lt "$deadline" ]; do
        tmp="$out_file.tmp"
        : >"$tmp"
        for host_octet in $(seq 20 199); do
            ipaddr="$prefix.$host_octet"
            ping -W 1 -c 1 "$ipaddr" >/dev/null 2>&1 || true
            if curl -fsS --max-time 2 "http://$ipaddr:8080/status" >"$LOG_DIR/status-$ipaddr.json" 2>/dev/null; then
                printf '%s\n' "$ipaddr" >>"$tmp"
            fi
        done
        while read -r ipaddr _; do
            [ -n "$ipaddr" ] || continue
            case "$ipaddr" in
                "${LINUX_AP_ADDR%/*}"|169.254.*) continue ;;
            esac
            if curl -fsS --max-time 4 "http://$ipaddr:8080/status" >"$LOG_DIR/status-$ipaddr.json" 2>/dev/null; then
                printf '%s\n' "$ipaddr" >>"$tmp"
            fi
        done < <(ip -4 neigh show dev "$iface" | awk '{print $1, $0}')
        sort -u "$tmp" >"$out_file"
        if [ "$(wc -l <"$out_file")" -ge "$count" ]; then
            rm -f "$tmp"
            return 0
        fi
        rm -f "$tmp"
        sleep 2
    done
    return 1
}

post_peer() {
    node=$1
    idx=$2
    name=$3
    host=$4
    attempt=1
    while [ "$attempt" -le 3 ]; do
        if curl -fsS --max-time 15 -X POST -H 'Content-Type: application/json' \
            --data "{\"name\":\"$name\",\"host\":\"$host\",\"mqtt_port\":$MQTT_PORT,\"p2p_port\":$P2P_PORT,\"enabled\":1}" \
            "http://$node:8080/peers/$idx" >"$LOG_DIR/config-peer-$node-$idx.json"; then
            return 0
        fi
        printf 'warning: peer config failed node=%s idx=%s attempt=%s/3\n' \
            "$node" "$idx" "$attempt" >&2
        sleep 2
        attempt=$((attempt + 1))
    done
    return 1
}

wait_peer_count() {
    node=$1
    min_count=$2
    deadline=$((SECONDS + SETTLE_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        status=$(curl -fsS --max-time 4 "http://$node:8080/status" || true)
        count=$(printf '%s\n' "$status" | sed -n 's/.*"connected_peers":\([0-9][0-9]*\).*/\1/p')
        if [ -n "$count" ] && [ "$count" -ge "$min_count" ]; then
            printf '%s\n' "$status" >"$LOG_DIR/status-peer-ok-$node.json"
            return 0
        fi
        sleep 1
    done
    curl -fsS --max-time 4 "http://$node:8080/status" >"$LOG_DIR/status-peer-timeout-$node.json" || true
    return 1
}

wait_for_payload() {
    file=$1
    payload=$2
    timeout=${3:-$WAIT_SECONDS}
    deadline=$((SECONDS + timeout))
    while [ "$SECONDS" -lt "$deadline" ]; do
        rg -q --fixed-strings "$payload" "$file" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

publish_until_received() {
    pub_host=$1
    sub_host=$2
    topic=$3
    payload=$4
    recv_file=$5

    rm -f "$recv_file"
    "$CLI" sub -h "$sub_host" -p "$MQTT_PORT" -t "$topic" >"$recv_file" 2>&1 &
    SUB_PID=$!
    sleep 4
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$CLI" pub -h "$pub_host" -p "$MQTT_PORT" -t "$topic" -m "$payload" >/dev/null 2>&1 || true
        wait_for_payload "$recv_file" "$payload" 2 && return 0
        sleep 1
    done
    return 1
}

start_load_subscriber() {
    client_id=$1
    topic=$2
    chosen_file=$3
    shift 3

    for host in "$@"; do
        log_file="$LOG_DIR/load-sub-$client_id-$host-$STAMP.log"
        rm -f "$log_file"
        "$CLI" sub -h "$host" -p "$MQTT_PORT" -i "wifi_load_$client_id" -t "$topic" \
            >"$log_file" 2>&1 &
        pid=$!
        if wait_for_payload "$log_file" "subscribed" 4; then
            printf '%s %s %s\n' "$client_id" "$host" "$pid" >"$chosen_file"
            LOAD_PIDS+=("$pid")
            return 0
        fi
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
    return 1
}

publish_with_fallback() {
    topic=$1
    payload=$2
    shift 2

    for host in "$@"; do
        log_file="$LOG_DIR/load-pub-$host-$STAMP.log"
        if "$CLI" pub -h "$host" -p "$MQTT_PORT" -i wifi_load_pub -t "$topic" -m "$payload" \
            >"$log_file" 2>&1; then
            printf '%s\n' "$host"
            return 0
        fi
    done
    return 1
}

wait_for_all_load_payloads() {
    payload=$1
    shift
    deadline=$((SECONDS + LOAD_WAIT_SECONDS))

    while [ "$SECONDS" -lt "$deadline" ]; do
        missing=0
        for file in "$@"; do
            if ! rg -q --fixed-strings "$payload" "$file" 2>/dev/null; then
                missing=1
                break
            fi
        done
        [ "$missing" -eq 0 ] && return 0
        sleep 0.5
    done
    return 1
}

verify_load_distribution() {
    entry_host=$1
    shift
    hosts=("$entry_host" "$@")
    topic="$LOAD_TOPIC/$STAMP"
    chosen_files=()
    sub_logs=()

    printf 'entry broker: %s\n' "$entry_host"
    printf 'load clients: %s, required brokers: %s\n' "$LOAD_CLIENTS" "$LOAD_MIN_BROKERS"
    for idx in $(seq 1 "$LOAD_CLIENTS"); do
        chosen_file="$LOG_DIR/load-sub-$idx-chosen-$STAMP.txt"
        if ! start_load_subscriber "$idx" "$topic" "$chosen_file" "${hosts[@]}"; then
            printf 'error: load subscriber %s could not connect through fallback hosts\n' "$idx" >&2
            return 1
        fi
        chosen_files+=("$chosen_file")
        chosen_host=$(awk '{print $2}' "$chosen_file")
        sub_logs+=("$LOG_DIR/load-sub-$idx-$chosen_host-$STAMP.log")
    done

    awk '{print $2}' "${chosen_files[@]}" | sort | uniq -c | tee "$LOG_DIR/load-distribution-$STAMP.txt"
    used_brokers=$(awk '{print $2}' "${chosen_files[@]}" | sort -u | wc -l)
    if [ "$used_brokers" -lt "$LOAD_MIN_BROKERS" ]; then
        printf 'error: expected load across at least %s brokers, got %s\n' \
            "$LOAD_MIN_BROKERS" "$used_brokers" >&2
        return 1
    fi

    payload="wifi-load-distribution-$STAMP"
    pub_host=$(publish_with_fallback "$topic" "$payload" "${hosts[@]}") || {
        printf 'error: load publisher could not connect through fallback hosts\n' >&2
        return 1
    }
    printf 'publisher fallback host: %s\n' "$pub_host"
    wait_for_all_load_payloads "$payload" "${sub_logs[@]}"
}

wait_http_node() {
    node=$1
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if curl -fsS --max-time 4 "http://$node:8080/status" >"$LOG_DIR/status-$node.json" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_http_node_stable() {
    node=$1
    required=${2:-3}
    deadline=$((SECONDS + WAIT_SECONDS))
    ok_count=0

    if [ "$required" -le 0 ]; then
        return 0
    fi

    while [ "$SECONDS" -lt "$deadline" ]; do
        if curl -fsS --max-time 4 "http://$node:8080/status" >"$LOG_DIR/status-$node.json" 2>/dev/null; then
            ok_count=$((ok_count + 1))
            if [ "$ok_count" -ge "$required" ]; then
                return 0
            fi
            sleep 1
        else
            ok_count=0
            sleep 2
        fi
    done
    return 1
}

need curl
need ip
need nmcli
need python3
need rg
need ss

WIFI_IFACE=$(detect_wifi_iface)
[ -n "$WIFI_IFACE" ] || {
    printf 'error: no WiFi interface found; set WIFI_IFACE=<dev>\n' >&2
    exit 1
}

printf '=== WiFi Linux AP ESP32 bridge test ===\n'
printf 'node count:  %s\n' "$NODE_COUNT"
printf 'ports:       %s\n' "$ESP_PORTS"
printf 'node IPs:    %s\n' "${NODE_IPS:-auto}"
printf 'wifi iface:  %s\n' "$WIFI_IFACE"
printf 'Linux AP SSID: %s\n' "$LINUX_AP_SSID"
printf 'ESP32 mode:    WiFi STA on Linux-hosted AP\n'
printf 'AP address:  %s\n' "$LINUX_AP_ADDR"
printf 'run log:     %s\n' "$RUN_LOG"
printf 'erase config:%s\n' "$ERASE_CONFIG"
printf 'UART provision:%s\n' "$PROVISION_BY_UART"
printf 'broker enabled:%s\n' "$BROKER_ENABLED"
printf 'HTTP stable required:%s\n' "$HTTP_STABLE_REQUIRED"
printf 'UART peer config:%s\n' "$UART_PEER_CONFIG"
printf 'setup Linux AP:%s\n' "$SETUP_LINUX_AP"

if [ "$SKIP_BUILD" != "1" ]; then
    "$ROOT_DIR/scripts/sync_deps.sh" replace
    "$ROOT_DIR/scripts/build_wifi_bridge_product.sh"
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

if [ "$SKIP_FLASH" != "1" ]; then
    WEST_INFO=$(find_west_workspace)
    WEST=$(printf '%s\n' "$WEST_INFO" | sed -n '1p')
    WEST_WORKDIR=$(printf '%s\n' "$WEST_INFO" | sed -n '2p')
    if [ -d "$WEST_WORKDIR/.venv/bin" ]; then
        PATH="$WEST_WORKDIR/.venv/bin:$PATH"
        export PATH
    fi
fi

printf '\n[1/6] Start Linux-hosted AP for ESP32 STA nodes\n'
if [ "$SETUP_LINUX_AP" = "1" ]; then
    nmcli radio wifi on
    nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
    nmcli connection down "$LINUX_AP_CONN" >/dev/null 2>&1 || true
    nmcli connection delete "$LINUX_AP_CONN" >/dev/null 2>&1 || true
    nmcli connection add type wifi ifname "$WIFI_IFACE" con-name "$LINUX_AP_CONN" ssid "$LINUX_AP_SSID" >/dev/null
    nmcli connection modify "$LINUX_AP_CONN" \
        connection.autoconnect no \
        802-11-wireless.mode ap \
        802-11-wireless.band bg \
        802-11-wireless.channel "$LINUX_AP_CHANNEL" \
        802-11-wireless.powersave 2 \
        802-11-wireless.ap-isolation 0 \
        ipv4.method manual \
        ipv4.addresses "$LINUX_AP_ADDR" \
        ipv6.method ignore
    if [ -n "$LINUX_AP_PASS" ]; then
        nmcli connection modify "$LINUX_AP_CONN" \
            wifi-sec.key-mgmt wpa-psk \
            wifi-sec.pmf 1 \
            wifi-sec.psk "$LINUX_AP_PASS"
    fi
    nmcli connection up "$LINUX_AP_CONN" ifname "$WIFI_IFACE" >/dev/null
else
    printf 'SETUP_LINUX_AP=0; expecting existing AP ssid=%s addr=%s on %s\n' \
        "$LINUX_AP_SSID" "$LINUX_AP_ADDR" "$WIFI_IFACE"
fi
nmcli device show "$WIFI_IFACE" >"$LOG_DIR/linux-ap-device-$STAMP.txt"

printf '\n[2/6] Flash ESP32 WiFi STA nodes and provision static IPs/AP credentials\n'
if [ "$SKIP_FLASH" != "1" ]; then
    NODE_IPS=""
    # shellcheck disable=SC2206
    ESP_PORT_ARRAY=($ESP_PORTS)
    if [ "${#ESP_PORT_ARRAY[@]}" -lt "$NODE_COUNT" ]; then
        printf 'error: NODE_COUNT=%s requires at least %s ESP_PORTS entries\n' \
            "$NODE_COUNT" "$NODE_COUNT" >&2
        exit 1
    fi
    STATIC_IP_ARRAY=()
    for node_idx in $(seq 0 $((NODE_COUNT - 1))); do
        STATIC_IP_ARRAY+=("$(static_ip_for_node "$node_idx")")
    done
    node_idx=0
    for port in "${ESP_PORT_ARRAY[@]}"; do
        [ "$node_idx" -lt "$NODE_COUNT" ] || break
        [ -e "$port" ] || {
            printf 'error: missing ESP32 port: %s\n' "$port" >&2
            exit 1
        }
        printf 'Flashing %s\n' "$port"
        flash_node "$port" "$LOG_DIR/flash-$(basename "$port")-$STAMP.log"
        if [ "$ERASE_CONFIG" = "1" ]; then
            "$WEST_WORKDIR/.venv/bin/python3" -m esptool --port "$port" \
                erase-region "$CONFIG_ERASE_OFFSET" "$CONFIG_ERASE_SIZE" \
                2>&1 | tee "$LOG_DIR/erase-config-$(basename "$port")-$STAMP.log"
        fi
        node_ip=""
        if [ "$PROVISION_BY_UART" = "1" ]; then
            static_ip=${STATIC_IP_ARRAY[$node_idx]}
            peer_commands=$(peer_commands_for_node "$node_idx" "${STATIC_IP_ARRAY[@]}" | paste -sd '|' -)
            provision_log="$LOG_DIR/uart-provision-$(basename "$port")-$STAMP.log"

            printf 'UART provision %s static IP %s SSID %s\n' \
                "$port" "$static_ip" "$LINUX_AP_SSID"
            if [ -n "$peer_commands" ]; then
                printf 'UART peer commands for %s: %s\n' "$port" "$peer_commands"
            fi
            provision_node_uart "$port" "$static_ip" "$peer_commands" "$provision_log"
            node_ip=$static_ip
            if [ "$EARLY_RUNTIME_SERIAL" = "1" ]; then
                start_runtime_serial_log "$port" "$LOG_DIR/uart-runtime-$(basename "$port")-$STAMP.log"
            fi
            wait_http_node_stable "$node_ip" "$HTTP_STABLE_REQUIRED" || {
                printf 'error: provisioned node HTTP did not remain reachable: %s\n' "$node_ip" >&2
                exit 1
            }
        else
            boot_log="$LOG_DIR/uart-boot-$(basename "$port")-$STAMP.log"
            monitor_node_boot "$port" "$boot_log"
        fi
        if [ -z "$node_ip" ]; then
            node_ip=$(sed -n 's/.*STA static IPv4 \([0-9.][0-9.]*\) gw=.*/\1/p' "$boot_log" | tail -n1)
        fi
        if [ -z "$node_ip" ]; then
            node_ip=$(sed -n 's/.*broker startup requested: ip=\([0-9.][0-9.]*\) mqtt=.*/\1/p' "$boot_log" | tail -n1)
        fi
        if [ -z "$node_ip" ]; then
            boot_log=${boot_log:-"$LOG_DIR/uart-provision-$(basename "$port")-$STAMP.log"}
            printf 'error: could not parse static IP from %s\n' "$boot_log" >&2
            exit 1
        fi
        NODE_IPS="${NODE_IPS:+$NODE_IPS }$node_ip"
        node_idx=$((node_idx + 1))
    done
else
    printf 'SKIP_FLASH=1; using currently flashed nodes\n'
fi

printf '\n[3/6] Discover %s ESP32 STA node(s) on Linux-hosted AP\n' "$NODE_COUNT"
NODES_FILE="$LOG_DIR/nodes-$STAMP.txt"
if [ -n "$NODE_IPS" ]; then
    printf '%s\n' $NODE_IPS >"$NODES_FILE"
elif ! wait_for_nodes "$WIFI_IFACE" "$NODE_COUNT" "$NODES_FILE"; then
    printf 'error: did not discover %s HTTP nodes within %ss\n' "$NODE_COUNT" "$WAIT_SECONDS" >&2
    printf 'neighbor table:\n'
    ip -4 neigh show dev "$WIFI_IFACE" || true
    exit 1
fi

if [ "$BROKER_ENABLED" != "1" ]; then
    printf 'HTTP-only WiFi STA diagnostic passed for nodes:\n'
    cat "$NODES_FILE"
    exit 0
fi

mapfile -t NODES <"$NODES_FILE"
if [ "${#NODES[@]}" -lt "$NODE_COUNT" ]; then
    printf 'error: expected %s nodes, found %s\n' "$NODE_COUNT" "${#NODES[@]}" >&2
    cat "$NODES_FILE" >&2
    exit 1
fi
printf 'nodes:\n'
printf '  %s\n' "${NODES[@]:0:$NODE_COUNT}"
for node in "${NODES[@]:0:$NODE_COUNT}"; do
    wait_http_node_stable "$node" "$HTTP_STABLE_REQUIRED" || {
        printf 'error: node HTTP did not remain reachable: %s\n' "$node" >&2
        exit 1
    }
done
if [ "$RUNTIME_SERIAL" = "1" ]; then
    # shellcheck disable=SC2206
    ESP_PORT_ARRAY=($ESP_PORTS)
    for port in "${ESP_PORT_ARRAY[@]:0:$NODE_COUNT}"; do
        start_runtime_serial_log "$port" "$LOG_DIR/uart-runtime-$(basename "$port")-$STAMP.log"
    done
fi

printf '\n[4/6] Configure chain bridge peers\n'
if [ "$UART_PEER_CONFIG" = "1" ]; then
    printf 'peer chain was configured over UART before reboot; settling %ss\n' "$SETTLE_SECONDS"
    sleep "$SETTLE_SECONDS"
else
    for node in "${NODES[@]:0:$NODE_COUNT}"; do
        wait_http_node "$node" || {
            printf 'error: node HTTP became unreachable before peer config: %s\n' "$node" >&2
            exit 1
        }
    done
    for idx in $(seq 0 $((NODE_COUNT - 1))); do
        peer_idx=0
        if [ "$idx" -gt 0 ]; then
            prev=$((idx - 1))
            post_peer "${NODES[$idx]}" "$peer_idx" "node$prev" "${NODES[$prev]}"
            peer_idx=$((peer_idx + 1))
        fi
        if [ "$idx" -lt $((NODE_COUNT - 1)) ]; then
            next=$((idx + 1))
            post_peer "${NODES[$idx]}" "$peer_idx" "node$next" "${NODES[$next]}"
        fi
    done

    for idx in $(seq 0 $((NODE_COUNT - 1))); do
        wait_peer_count "${NODES[$idx]}" "$(expected_peer_count_for_node "$idx" "$NODE_COUNT")"
    done
fi

last_idx=$((NODE_COUNT - 1))

printf '\n[5/6] Verify bidirectional MQTT delivery across the %s-node bridge\n' "$NODE_COUNT"
topic_a="site/field-a/wifi-ap/0-to-$last_idx"
payload_a="wifi-bridge-0-to-$last_idx-$STAMP"
publish_until_received "${NODES[0]}" "${NODES[$last_idx]}" "$topic_a" "$payload_a" "$LOG_DIR/recv-0-to-$last_idx-$STAMP.log"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

topic_b="site/field-a/wifi-ap/$last_idx-to-0"
payload_b="wifi-bridge-$last_idx-to-0-$STAMP"
publish_until_received "${NODES[$last_idx]}" "${NODES[0]}" "$topic_b" "$payload_b" "$LOG_DIR/recv-$last_idx-to-0-$STAMP.log"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

printf '\n[6/6] Verify Linux client fallback load distribution from one WiFi broker\n'
verify_load_distribution "${NODES[@]:0:$NODE_COUNT}"

printf '\nPASS: %s ESP32 WiFi broker bridge node(s) exchanged MQTT messages on %s\n' "$NODE_COUNT" "$LINUX_AP_SSID"
