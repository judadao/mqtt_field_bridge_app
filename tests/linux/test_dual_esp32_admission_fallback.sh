#!/usr/bin/env bash
# Validate Mode A fallback between two W5500 ESP32 brokers.
# ESP B admits two local MQTT clients, rejects the third, and the third falls
# back to ESP A while bridge delivery to ESP B's existing clients still works.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/dual_esp32_admission_fallback"

ESP_A_HTTP=${ESP_A_HTTP:-192.168.127.4}
ESP_A_BROKER=${ESP_A_BROKER:-192.168.127.15}
ESP_B_HTTP=${ESP_B_HTTP:-192.168.127.6}
ESP_B_BROKER=${ESP_B_BROKER:-192.168.127.16}
MQTT_PORT=${MQTT_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}
TOPIC=${TOPIC:-site/field-a/esp32-fallback}
SETTLE_SEC=${SETTLE_SEC:-15}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-18}
SUB_PROP_SEC=${SUB_PROP_SEC:-10}
IFACE=${IFACE:-}

PASS=0
FAIL=0
SUB_PIDS=()

mkdir -p "$OUT_DIR"

pass() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1" >&2; }

cleanup() {
    for pid in "${SUB_PIDS[@]}"; do
        kill "$pid" >/dev/null 2>&1 || true
    done
    wait "${SUB_PIDS[@]}" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'error: missing command: %s\n' "$1" >&2
        exit 1
    }
}

detect_iface() {
    if [ -n "$IFACE" ]; then
        printf '%s\n' "$IFACE"
        return 0
    fi
    ip route get "$ESP_A_HTTP" | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n1
}

http_json() {
    method=$1
    url=$2
    body=${3:-}
    if [ -n "$body" ]; then
        curl -fsS --max-time 6 -X "$method" -H 'Content-Type: application/json' \
            --data "$body" "$url"
    else
        curl -fsS --max-time 6 -X "$method" "$url"
    fi
}

wait_for_http() {
    host=$1
    deadline=$((SECONDS + WAIT_MSG_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS --max-time 3 "http://$host:8080/status" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

wait_for_tcp() {
    host=$1
    port=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        timeout 2 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1 && return 0
        sleep 0.5
    done
    return 1
}

wait_for_match() {
    file=$1
    pattern=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        rg -q --fixed-strings "$pattern" "$file" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

wait_for_peer_count() {
    host=$1
    min_count=$2
    deadline=$((SECONDS + SETTLE_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        status=$(curl -fsS --max-time 3 "http://$host:8080/status" || true)
        count=$(printf '%s\n' "$status" | sed -n 's/.*"connected_peers":\([0-9][0-9]*\).*/\1/p')
        if [ -n "$count" ] && [ "$count" -ge "$min_count" ]; then
            printf '%s\n' "$status" >"$OUT_DIR/status-$host.json"
            return 0
        fi
        sleep 1
    done
    curl -fsS --max-time 3 "http://$host:8080/status" >"$OUT_DIR/status-$host-timeout.json" || true
    return 1
}

check_arp_mac() {
    ip_addr=$1
    unexpected=$2
    log="$OUT_DIR/arp-$ip_addr.log"

    timeout 4 arping -I "$IFACE" -c 2 "$ip_addr" >"$log" 2>&1 || return 1
    if rg -qi --fixed-strings "$unexpected" "$log"; then
        return 2
    fi
    return 0
}

configure_bridge() {
    http_json POST "http://$ESP_A_HTTP:8080/peers/0" \
        "{\"name\":\"esp-b\",\"host\":\"$ESP_B_BROKER\",\"mqtt_port\":$MQTT_PORT,\"enabled\":1}" \
        >"$OUT_DIR/config-peer-a.json"
    http_json POST "http://$ESP_B_HTTP:8080/peers/0" \
        "{\"name\":\"esp-a-disabled\",\"host\":\"$ESP_A_BROKER\",\"mqtt_port\":$MQTT_PORT,\"enabled\":0}" \
        >"$OUT_DIR/config-peer-b.json"
}

need arping
need curl
need ip
need rg

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

IFACE=$(detect_iface)
if [ -z "$IFACE" ]; then
    printf 'error: could not detect interface for %s; set IFACE=<dev>\n' "$ESP_A_HTTP" >&2
    exit 1
fi

printf '=== test_dual_esp32_admission_fallback.sh ===\n'
printf 'iface=%s\n' "$IFACE"
printf 'Fallback target ESP A: http=%s broker=%s admission=2\n' "$ESP_A_HTTP" "$ESP_A_BROKER"
printf 'Saturated ESP B: http=%s broker=%s admission=2\n' "$ESP_B_HTTP" "$ESP_B_BROKER"

wait_for_http "$ESP_A_HTTP" && pass "ESP A HTTP reachable" || fail "ESP A HTTP unreachable"
wait_for_http "$ESP_B_HTTP" && pass "ESP B HTTP reachable" || fail "ESP B HTTP unreachable"
check_arp_mac "$ESP_A_HTTP" "02:00:00:12:34:56" && pass "ESP A management MAC is not default duplicate" || fail "ESP A MAC check failed"
check_arp_mac "$ESP_B_HTTP" "02:00:00:12:34:56" && pass "ESP B management MAC is not default duplicate" || fail "ESP B MAC check failed"
wait_for_tcp "$ESP_A_BROKER" "$MQTT_PORT" 4 && pass "ESP A broker TCP open" || fail "ESP A broker TCP closed"
wait_for_tcp "$ESP_B_BROKER" "$MQTT_PORT" 4 && pass "ESP B broker TCP open" || fail "ESP B broker TCP closed"

configure_bridge
sleep "$SETTLE_SEC"
wait_for_peer_count "$ESP_A_HTTP" 1 && pass "ESP A sees ESP B bridge peer" || fail "ESP A peer did not connect"
wait_for_peer_count "$ESP_B_HTTP" 1 && pass "ESP B sees ESP A bridge peer" || fail "ESP B peer did not connect"

"$CLI" sub -h "$ESP_B_BROKER" -p "$MQTT_PORT" -i esp_b_fill_1 -t "$TOPIC" >"$OUT_DIR/esp-b-fill-1.log" 2>&1 &
SUB_PIDS+=($!)
"$CLI" sub -h "$ESP_B_BROKER" -p "$MQTT_PORT" -i esp_b_fill_2 -t "$TOPIC" >"$OUT_DIR/esp-b-fill-2.log" 2>&1 &
SUB_PIDS+=($!)
sleep "$SUB_PROP_SEC"

wait_for_match "$OUT_DIR/esp-b-fill-1.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "ESP B filler client 1 admitted" ||
    fail "ESP B filler client 1 not admitted"
wait_for_match "$OUT_DIR/esp-b-fill-2.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "ESP B filler client 2 admitted" ||
    fail "ESP B filler client 2 not admitted"

reject_out=$("$CLI" sub -h "$ESP_B_BROKER" -p "$MQTT_PORT" -i esp_b_overflow -t "$TOPIC" 2>&1 >/dev/null || true)
printf '%s\n' "$reject_out" >"$OUT_DIR/esp-b-overflow.err"
if printf '%s\n' "$reject_out" | rg -qi "server unavailable|code 3"; then
    pass "ESP B rejects overflow client with server-unavailable"
else
    fail "ESP B overflow client was not rejected as server-unavailable"
fi

"$CLI" sub -h "$ESP_A_BROKER" -p "$MQTT_PORT" -i fallback_on_esp_a -t "$TOPIC" >"$OUT_DIR/esp-a-fallback.log" 2>&1 &
SUB_PIDS+=($!)
wait_for_match "$OUT_DIR/esp-a-fallback.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "overflow client falls back to ESP A" ||
    fail "overflow client did not subscribe on ESP A"
sleep "$SUB_PROP_SEC"

msg="esp32-fallback-$(date +%s)"
"$CLI" pub -h "$ESP_A_BROKER" -p "$MQTT_PORT" -i esp_a_pub -t "$TOPIC" -m "$msg" >/dev/null 2>&1

wait_for_match "$OUT_DIR/esp-a-fallback.log" "$msg" "$WAIT_MSG_SEC" &&
    pass "ESP A fallback client receives local publish" ||
    fail "ESP A fallback client missed local publish"
wait_for_match "$OUT_DIR/esp-b-fill-1.log" "$msg" "$WAIT_MSG_SEC" &&
    pass "ESP B existing client receives bridged publish from ESP A" ||
    fail "ESP B existing client missed bridged publish"

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
