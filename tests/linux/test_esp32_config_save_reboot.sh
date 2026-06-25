#!/usr/bin/env bash
# Validate that ESP32 web config save persists, reboots, and changes broker bind.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/hardware"

ESP32_DEVICE_IP=${ESP32_DEVICE_IP:-192.168.127.4}
ESP32_DEFAULT_BROKER_IP=${ESP32_DEFAULT_BROKER_IP:-192.168.127.15}
ESP32_TEST_BROKER_IP=${ESP32_TEST_BROKER_IP:-192.168.127.16}
ESP32_GATEWAY=${ESP32_GATEWAY:-192.168.127.5}
ESP32_NETMASK=${ESP32_NETMASK:-255.255.0.0}
ESP32_DNS=${ESP32_DNS:-192.168.127.5}
MQTT_PORT=${MQTT_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}
HTTP_PORT=${HTTP_PORT:-8080}
TOPIC=${TOPIC:-site/field-a/test}
MSG=${MSG:-config-save-reboot-check}
IFACE=${IFACE:-}

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

wait_http() {
    local deadline
    deadline=$((SECONDS + 45))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if curl -fsS --max-time 3 "http://$ESP32_DEVICE_IP:$HTTP_PORT/status" >/dev/null; then
            return 0
        fi
        sleep 1
    done
    return 1
}

post_json() {
    local path=$1
    local body=$2
    curl -fsS --max-time 8 \
        -H 'Content-Type: application/json' \
        -X POST \
        --data "$body" \
        "http://$ESP32_DEVICE_IP:$HTTP_PORT$path"
}

config_body() {
    local broker_ip=$1
    printf '{"device_name":"esp32-min-broker","device_ip":"%s","gateway":"%s","netmask":"%s","dns":"%s","dhcp_enabled":0,"broker_ip":"%s","site_id":"field-a","topic_prefix":"site/field-a","mqtt_port":%s,"p2p_port":%s,"broker_enabled":1,"bridge_enabled":1,"mesh_enabled":1}' \
        "$ESP32_DEVICE_IP" "$ESP32_GATEWAY" "$ESP32_NETMASK" "$ESP32_DNS" \
        "$broker_ip" "$MQTT_PORT" "$P2P_PORT"
}

request_reboot() {
    curl -fsS --max-time 5 -X POST "http://$ESP32_DEVICE_IP:$HTTP_PORT/reboot" >/dev/null
}

reboot_and_wait() {
    request_reboot
    sleep 5
    wait_http
}

mqtt_round_trip() {
    local host=$1
    local msg=$2
    local log=$3
    rm -f "$log"
    timeout 8 "$CLI" sub -h "$host" -p "$MQTT_PORT" -t "$TOPIC" > "$log" 2>&1 &
    local sub_pid=$!
    sleep 1
    "$CLI" pub -h "$host" -p "$MQTT_PORT" -t "$TOPIC" -m "$msg" >/dev/null
    wait "$sub_pid" || true
    rg -q "$msg" "$log"
}

need_tool ip
need_tool curl
need_tool timeout
need_tool rg
need_tool arping

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

IFACE=$(detect_iface)
if [ -z "$IFACE" ]; then
    fail "could not detect interface for $ESP32_DEVICE_IP; set IFACE=<dev>"
fi

STAMP=$(date +%Y%m%d-%H%M%S)
SUB_LOG="$OUT_DIR/config-save-reboot-sub-$STAMP.log"

printf '=== test_esp32_config_save_reboot.sh ===\n'
printf 'iface=%s device=%s default_broker=%s test_broker=%s\n' \
    "$IFACE" "$ESP32_DEVICE_IP" "$ESP32_DEFAULT_BROKER_IP" "$ESP32_TEST_BROKER_IP"

wait_http || fail "HTTP status did not become reachable on $ESP32_DEVICE_IP:$HTTP_PORT"
pass "HTTP status reachable"

post_json /config "$(config_body "$ESP32_DEFAULT_BROKER_IP")" | rg -q '"status":"ok"' ||
    fail "default config save failed"
reboot_and_wait || fail "HTTP did not return after default config reboot"
pass "default config saved and rebooted"

timeout 5 arping -I "$IFACE" -c 2 "$ESP32_DEFAULT_BROKER_IP" >/dev/null ||
    fail "ARP failed for default broker IP $ESP32_DEFAULT_BROKER_IP"
mqtt_round_trip "$ESP32_DEFAULT_BROKER_IP" "$MSG-default" "$SUB_LOG" ||
    fail "MQTT round-trip failed on default broker IP"
pass "default broker IP works before change"

post_json /config "$(config_body "$ESP32_TEST_BROKER_IP")" | rg -q '"reboot_required":true' ||
    fail "test broker config save did not report reboot_required"
reboot_and_wait || fail "HTTP did not return after test broker config reboot"
pass "test broker config saved and rebooted"

timeout 5 arping -I "$IFACE" -c 2 "$ESP32_TEST_BROKER_IP" >/dev/null ||
    fail "ARP failed for test broker IP $ESP32_TEST_BROKER_IP"
mqtt_round_trip "$ESP32_TEST_BROKER_IP" "$MSG-test" "$SUB_LOG" ||
    fail "MQTT round-trip failed on test broker IP"
pass "new broker IP works after save and reboot"

if timeout 5 "$CLI" pub -h "$ESP32_DEFAULT_BROKER_IP" -p "$MQTT_PORT" -t "$TOPIC" -m should-fail >/tmp/esp32-old-broker-mqtt.log 2>&1; then
    fail "MQTT unexpectedly accepted on old broker IP $ESP32_DEFAULT_BROKER_IP"
fi
pass "old broker IP no longer accepts MQTT"

post_json /config "$(config_body "$ESP32_DEFAULT_BROKER_IP")" | rg -q '"reboot_required":true' ||
    fail "restore default broker config save failed"
reboot_and_wait || fail "HTTP did not return after restore reboot"
pass "default broker IP restored"

printf '\nConfig save reboot validation passed.\n'
