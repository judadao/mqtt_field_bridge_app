#!/usr/bin/env bash
# Validate ESP32 Ethernet addressing and MQTT bind behavior.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/hardware"

ESP32_DEVICE_IP=${ESP32_DEVICE_IP:-192.168.127.4}
ESP32_BROKER_IP=${ESP32_BROKER_IP:-192.168.127.15}
MQTT_PORT=${MQTT_PORT:-1883}
HTTP_PORT=${HTTP_PORT:-8080}
TOPIC=${TOPIC:-site/field-a/test}
MSG=${MSG:-network-bind-check}
IFACE=${IFACE:-}
CAPTURE=${CAPTURE:-1}
HTTP_REQUIRED=${HTTP_REQUIRED:-1}

mkdir -p "$OUT_DIR"

need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'error: missing %s; run ./scripts/install_network_test_tools.sh\n' "$1" >&2
        exit 1
    fi
}

detect_iface() {
    if [ -n "$IFACE" ]; then
        printf '%s\n' "$IFACE"
        return 0
    fi
    ip route get "$ESP32_DEVICE_IP" | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n1
}

pass() {
    printf '  PASS  %s\n' "$1"
}

fail() {
    printf '  FAIL  %s\n' "$1" >&2
    exit 1
}

tcpdump_can_capture() {
    local probe_log="$OUT_DIR/tcpdump-probe.log"
    rm -f "$probe_log"
    timeout 1 tcpdump -i "$IFACE" -w /dev/null \
        "arp or host $ESP32_DEVICE_IP or host $ESP32_BROKER_IP" \
        >"$probe_log" 2>&1
    local rc=$?
    case "$rc" in
        0|124|143)
            return 0
            ;;
        *)
            cat "$probe_log" >&2 || true
            return 1
            ;;
    esac
}

need_tool ip
need_tool ping
need_tool arping
need_tool curl
need_tool timeout
need_tool rg

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

IFACE=$(detect_iface)
if [ -z "$IFACE" ]; then
    fail "could not detect interface for $ESP32_DEVICE_IP; set IFACE=<dev>"
fi

STAMP=$(date +%Y%m%d-%H%M%S)
CAPTURE_LOG="$OUT_DIR/network-bind-$STAMP.pcap"
SUB_LOG="$OUT_DIR/network-bind-sub-$STAMP.log"

printf '=== test_esp32_network_bind.sh ===\n'
printf 'iface=%s device=%s broker=%s mqtt=%s http=%s\n' \
    "$IFACE" "$ESP32_DEVICE_IP" "$ESP32_BROKER_IP" "$MQTT_PORT" "$HTTP_PORT"

TCPDUMP_PID=
if [ "$CAPTURE" = 1 ] && command -v tcpdump >/dev/null 2>&1; then
    if tcpdump_can_capture; then
        timeout 30 tcpdump -i "$IFACE" -w "$CAPTURE_LOG" \
            "arp or host $ESP32_DEVICE_IP or host $ESP32_BROKER_IP" >/dev/null 2>&1 &
        TCPDUMP_PID=$!
        printf 'capture=%s\n' "$CAPTURE_LOG"
    else
        printf 'warn: tcpdump is installed but cannot capture; run ./scripts/install_network_test_tools.sh\n' >&2
    fi
fi

cleanup() {
    if [ -n "${TCPDUMP_PID:-}" ]; then
        kill "$TCPDUMP_PID" >/dev/null 2>&1 || true
        wait "$TCPDUMP_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

ip neigh flush "$ESP32_DEVICE_IP" >/dev/null 2>&1 || true
ip neigh flush "$ESP32_BROKER_IP" >/dev/null 2>&1 || true

timeout 5 arping -I "$IFACE" -c 2 "$ESP32_DEVICE_IP" >/dev/null ||
    fail "ARP failed for management IP $ESP32_DEVICE_IP"
pass "ARP resolves management IP"

timeout 5 arping -I "$IFACE" -c 2 "$ESP32_BROKER_IP" >/dev/null ||
    fail "ARP failed for broker IP $ESP32_BROKER_IP"
pass "ARP resolves broker IP"

ping -c 1 -W 2 "$ESP32_DEVICE_IP" >/dev/null ||
    fail "ping failed for management IP $ESP32_DEVICE_IP"
pass "management IP ping"

ping -c 1 -W 2 "$ESP32_BROKER_IP" >/dev/null ||
    fail "ping failed for broker IP $ESP32_BROKER_IP"
pass "broker IP ping"

if [ "$HTTP_REQUIRED" = 1 ]; then
    curl -fsS --max-time 5 "http://$ESP32_DEVICE_IP:$HTTP_PORT/status" >/dev/null ||
        fail "HTTP status failed on management IP $ESP32_DEVICE_IP:$HTTP_PORT"
    pass "HTTP status on management IP"
fi

rm -f "$SUB_LOG"
timeout 8 "$CLI" sub -h "$ESP32_BROKER_IP" -p "$MQTT_PORT" -t "$TOPIC" > "$SUB_LOG" 2>&1 &
SUB_PID=$!
sleep 1
"$CLI" pub -h "$ESP32_BROKER_IP" -p "$MQTT_PORT" -t "$TOPIC" -m "$MSG" >/dev/null
wait "$SUB_PID" || true
rg -q "$MSG" "$SUB_LOG" ||
    fail "MQTT round-trip failed on broker IP $ESP32_BROKER_IP:$MQTT_PORT"
pass "MQTT round-trip on broker IP"

if timeout 5 "$CLI" pub -h "$ESP32_DEVICE_IP" -p "$MQTT_PORT" -t "$TOPIC" -m should-fail >/tmp/esp32-device-mqtt.log 2>&1; then
    fail "MQTT unexpectedly accepted on management IP $ESP32_DEVICE_IP:$MQTT_PORT"
fi
pass "MQTT is not listening on management IP"

printf '\nNetwork bind validation passed.\n'
