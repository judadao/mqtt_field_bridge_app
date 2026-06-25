#!/usr/bin/env bash
# Validate two W5500 ESP32 field bridge nodes as bidirectional MQTT bridge peers.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/dual_w5500_bridge"

ESP_A_HTTP=${ESP_A_HTTP:-192.168.127.4}
ESP_A_BROKER=${ESP_A_BROKER:-192.168.127.15}
ESP_B_HTTP=${ESP_B_HTTP:-192.168.127.6}
ESP_B_BROKER=${ESP_B_BROKER:-192.168.127.16}
MQTT_PORT=${MQTT_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}
SETTLE_SEC=${SETTLE_SEC:-10}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-12}
SUB_PROP_SEC=${SUB_PROP_SEC:-8}
TOPIC=${TOPIC:-site/field-a/dual-w5500}
IFACE=${IFACE:-}
DUAL_SEED=${DUAL_SEED:-0}

PASS=0
FAIL=0
SUB_PID=""

mkdir -p "$OUT_DIR"

pass() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1" >&2; }

cleanup() {
    if [ -n "$SUB_PID" ]; then
        kill "$SUB_PID" >/dev/null 2>&1 || true
        wait "$SUB_PID" >/dev/null 2>&1 || true
    fi
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
        if curl -fsS --max-time 3 "http://$host:8080/status" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
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

wait_for_match() {
    file=$1
    pattern=$2
    timeout=$3
    deadline=$((SECONDS + timeout))
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

configure_peer() {
    http_host=$1
    peer_name=$2
    peer_host=$3

    http_json POST "http://$http_host:8080/peers/0" \
        "{\"name\":\"$peer_name\",\"host\":\"$peer_host\",\"mqtt_port\":$MQTT_PORT,\"enabled\":1}" \
        >"$OUT_DIR/config-peer-$http_host.json"
}

need curl
need ip
need rg
need arping

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

IFACE=$(detect_iface)
if [ -z "$IFACE" ]; then
    printf 'error: could not detect interface for %s; set IFACE=<dev>\n' "$ESP_A_HTTP" >&2
    exit 1
fi

printf '=== test_dual_esp32_w5500_bridge.sh ===\n'
printf 'iface=%s\n' "$IFACE"
printf 'ESP A: http=%s broker=%s mqtt=%s p2p=%s\n' "$ESP_A_HTTP" "$ESP_A_BROKER" "$MQTT_PORT" "$P2P_PORT"
printf 'ESP B: http=%s broker=%s mqtt=%s p2p=%s\n' "$ESP_B_HTTP" "$ESP_B_BROKER" "$MQTT_PORT" "$P2P_PORT"
printf 'Dual seed: %s\n' "$DUAL_SEED"

wait_for_http "$ESP_A_HTTP" && pass "ESP A HTTP reachable" || fail "ESP A HTTP unreachable"
wait_for_http "$ESP_B_HTTP" && pass "ESP B HTTP reachable" || fail "ESP B HTTP unreachable"

check_arp_mac "$ESP_A_HTTP" "02:00:00:12:34:56" && pass "ESP A management MAC is not default duplicate" ||
    fail "ESP A management MAC check failed"
check_arp_mac "$ESP_B_HTTP" "02:00:00:12:34:56" && pass "ESP B management MAC is not default duplicate" ||
    fail "ESP B management MAC check failed"

timeout 2 bash -c "</dev/tcp/$ESP_A_BROKER/$MQTT_PORT" && pass "ESP A broker TCP open" ||
    fail "ESP A broker TCP closed"
timeout 2 bash -c "</dev/tcp/$ESP_B_BROKER/$MQTT_PORT" && pass "ESP B broker TCP open" ||
    fail "ESP B broker TCP closed"

printf '\nConfiguring bridge peers...\n'
configure_peer "$ESP_A_HTTP" "esp-b" "$ESP_B_BROKER"
if [ "$DUAL_SEED" = "1" ]; then
    configure_peer "$ESP_B_HTTP" "esp-a" "$ESP_A_BROKER"
else
    http_json POST "http://$ESP_B_HTTP:8080/peers/0" \
        "{\"name\":\"esp-a\",\"host\":\"$ESP_A_BROKER\",\"mqtt_port\":$MQTT_PORT,\"enabled\":0}" \
        >"$OUT_DIR/config-peer-$ESP_B_HTTP.json"
fi
sleep "$SETTLE_SEC"

wait_for_peer_count "$ESP_A_HTTP" 1 && pass "ESP A sees at least one connected peer" ||
    fail "ESP A peer did not connect"
wait_for_peer_count "$ESP_B_HTTP" 1 && pass "ESP B sees at least one connected peer" ||
    fail "ESP B peer did not connect"

topic_ab="$TOPIC/a-to-b"
msg_ab="esp-a-to-esp-b-$(date +%s)"
recv_ab="$OUT_DIR/recv-b.log"
rm -f "$recv_ab"
"$CLI" sub -h "$ESP_B_BROKER" -p "$MQTT_PORT" -t "$topic_ab" >"$recv_ab" 2>&1 &
SUB_PID=$!
sleep "$SUB_PROP_SEC"
publish_until_match "$ESP_A_BROKER" "$topic_ab" "$msg_ab" "$recv_ab" &&
    pass "ESP B receives publish sent to ESP A broker" ||
    fail "ESP B did not receive ESP A publish"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

topic_ba="$TOPIC/b-to-a"
msg_ba="esp-b-to-esp-a-$(date +%s)"
recv_ba="$OUT_DIR/recv-a.log"
rm -f "$recv_ba"
"$CLI" sub -h "$ESP_A_BROKER" -p "$MQTT_PORT" -t "$topic_ba" >"$recv_ba" 2>&1 &
SUB_PID=$!
sleep "$SUB_PROP_SEC"
publish_until_match "$ESP_B_BROKER" "$topic_ba" "$msg_ba" "$recv_ba" &&
    pass "ESP A receives publish sent to ESP B broker" ||
    fail "ESP A did not receive ESP B publish"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
