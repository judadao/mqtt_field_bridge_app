#!/usr/bin/env bash
# Validate a two-hop Ethernet bridge:
#   ESP A broker <-> Linux middle broker <-> ESP B broker
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/esp32_linux_esp32_bridge"
BROKER_BIN="$OUT_DIR/linux_middle_broker"

ESP_A_HTTP=${ESP_A_HTTP:-192.168.127.4}
ESP_A_BROKER=${ESP_A_BROKER:-192.168.127.15}
ESP_B_HTTP=${ESP_B_HTTP:-192.168.127.6}
ESP_B_BROKER=${ESP_B_BROKER:-192.168.127.16}
ESP_MQTT_PORT=${ESP_MQTT_PORT:-1883}
ESP_P2P_PORT=${ESP_P2P_PORT:-4884}
LINUX_HOST=${LINUX_HOST:-192.168.127.5}
LINUX_MQTT_PORT=${LINUX_MQTT_PORT:-21883}
LINUX_P2P_PORT=${LINUX_P2P_PORT:-24884}
DISC_PORT=${DISC_PORT:-19860}
SETTLE_SEC=${SETTLE_SEC:-15}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-24}
SUB_PROP_SEC=${SUB_PROP_SEC:-10}
TOPIC=${TOPIC:-site/field-a/linux-middle}
IFACE=${IFACE:-}
BRIDGE_MODE=${BRIDGE_MODE:-star}

PASS=0
FAIL=0
LINUX_PID=""
SUB_PID=""

mkdir -p "$OUT_DIR"

pass() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1" >&2; }

cleanup() {
    if [ -n "$SUB_PID" ]; then
        kill "$SUB_PID" >/dev/null 2>&1 || true
        wait "$SUB_PID" >/dev/null 2>&1 || true
    fi
    if [ -n "$LINUX_PID" ]; then
        kill "$LINUX_PID" >/dev/null 2>&1 || true
        wait "$LINUX_PID" >/dev/null 2>&1 || true
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

wait_for_tcp() {
    host=$1
    port=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if timeout 2 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

wait_for_mqtt() {
    host=$1
    port=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$CLI" status -h "$host" -p "$port" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
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

wait_for_peer_count() {
    http_host=$1
    min_count=$2
    deadline=$((SECONDS + SETTLE_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        status=$(curl -fsS --max-time 3 "http://$http_host:8080/status" || true)
        count=$(printf '%s\n' "$status" | sed -n 's/.*"connected_peers":\([0-9][0-9]*\).*/\1/p')
        if [ -n "$count" ] && [ "$count" -ge "$min_count" ]; then
            printf '%s\n' "$status" >"$OUT_DIR/status-$http_host.json"
            return 0
        fi
        sleep 1
    done
    curl -fsS --max-time 3 "http://$http_host:8080/status" >"$OUT_DIR/status-$http_host-timeout.json" || true
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

publish_until_match() {
    host=$1
    port=$2
    topic=$3
    payload=$4
    file=$5
    deadline=$((SECONDS + WAIT_MSG_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$CLI" pub -h "$host" -p "$port" -t "$topic" -m "$payload" >/dev/null 2>&1 || true
        wait_for_match "$file" "$payload" 1 && return 0
        sleep 0.5
    done
    return 1
}

build_middle_broker() {
    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DCONFIG_MQTT_P2P_DYNAMIC \
        -DCONFIG_MQTT_P2P_STATIC_SEEDS_ONLY \
        -DMQTT_BROKER_PORT="$LINUX_MQTT_PORT" \
        -DP2P_PORT="$LINUX_P2P_PORT" \
        -DP2P_DISCOVERY_PORT="$DISC_PORT" \
        -I"$BROKER_DIR/include" -I"$BROKER_DIR" \
        "$BROKER_DIR/src/broker.c" "$BROKER_DIR/src/client.c" \
        "$BROKER_DIR/src/main.c" "$BROKER_DIR/src/packet.c" \
        "$BROKER_DIR/src/session.c" "$BROKER_DIR/src/topic.c" \
        "$BROKER_DIR/src/p2p_discover.c" "$BROKER_DIR/src/p2p_election.c" \
        "$BROKER_DIR/src/p2p_peer.c" "$BROKER_DIR/src/p2p_router.c" \
        "$BROKER_DIR/src/p2p_shard.c" \
        "$BROKER_DIR/platform/posix/platform_posix.c" \
        -o "$BROKER_BIN" -lpthread
}

configure_esp_peer() {
    http_host=$1
    peer_name=$2
    peer_host=$3
    peer_mqtt_port=$4
    enabled=$5

    http_json POST "http://$http_host:8080/peers/0" \
        "{\"name\":\"$peer_name\",\"host\":\"$peer_host\",\"mqtt_port\":$peer_mqtt_port,\"enabled\":$enabled}" \
        >"$OUT_DIR/config-peer-$http_host.json"
}

need curl
need gcc
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

printf '=== test_esp32_linux_esp32_bridge.sh ===\n'
printf 'iface=%s\n' "$IFACE"
printf 'ESP A: http=%s broker=%s mqtt=%s p2p=%s\n' "$ESP_A_HTTP" "$ESP_A_BROKER" "$ESP_MQTT_PORT" "$ESP_P2P_PORT"
printf 'Linux middle: host=%s mqtt=%s p2p=%s\n' "$LINUX_HOST" "$LINUX_MQTT_PORT" "$LINUX_P2P_PORT"
printf 'ESP B: http=%s broker=%s mqtt=%s p2p=%s\n' "$ESP_B_HTTP" "$ESP_B_BROKER" "$ESP_MQTT_PORT" "$ESP_P2P_PORT"
printf 'Bridge mode: %s\n' "$BRIDGE_MODE"

wait_for_http "$ESP_A_HTTP" && pass "ESP A HTTP reachable" || fail "ESP A HTTP unreachable"
wait_for_http "$ESP_B_HTTP" && pass "ESP B HTTP reachable" || fail "ESP B HTTP unreachable"
wait_for_mqtt "$ESP_A_BROKER" "$ESP_MQTT_PORT" 6 && pass "ESP A broker MQTT reachable" || fail "ESP A broker MQTT unreachable"
wait_for_mqtt "$ESP_B_BROKER" "$ESP_MQTT_PORT" 6 && pass "ESP B broker MQTT reachable" || fail "ESP B broker MQTT unreachable"

printf '\nStarting Linux middle broker...\n'
build_middle_broker
fuser -k "$LINUX_MQTT_PORT/tcp" "$LINUX_P2P_PORT/tcp" >/dev/null 2>&1 || true
if [ "$BRIDGE_MODE" = "chain" ]; then
    linux_peers="$ESP_B_BROKER:$ESP_P2P_PORT"
else
    linux_peers=""
fi
MQTT_P2P_PEERS="$linux_peers" "$BROKER_BIN" >"$OUT_DIR/linux-middle.log" 2>&1 &
LINUX_PID=$!
wait_for_tcp 127.0.0.1 "$LINUX_MQTT_PORT" "$WAIT_MSG_SEC" &&
    pass "Linux middle broker TCP open" ||
    fail "Linux middle broker TCP closed"

printf '\nConfiguring bridge path ESP A -> Linux -> ESP B...\n'
configure_esp_peer "$ESP_A_HTTP" "linux-middle" "$LINUX_HOST" "$LINUX_MQTT_PORT" 1
if [ "$BRIDGE_MODE" = "chain" ]; then
    configure_esp_peer "$ESP_B_HTTP" "disabled-linux-middle" "$LINUX_HOST" "$LINUX_MQTT_PORT" 0
else
    configure_esp_peer "$ESP_B_HTTP" "linux-middle" "$LINUX_HOST" "$LINUX_MQTT_PORT" 1
fi
sleep "$SETTLE_SEC"

wait_for_peer_count "$ESP_A_HTTP" 1 && pass "ESP A sees Linux middle peer" || fail "ESP A peer did not connect"
wait_for_peer_count "$ESP_B_HTTP" 1 && pass "ESP B sees Linux middle peer" || fail "ESP B peer did not connect"

topic_ab="$TOPIC/a-to-b"
msg_ab="esp-a-linux-esp-b-$(date +%s)"
recv_ab="$OUT_DIR/recv-b.log"
rm -f "$recv_ab"
"$CLI" sub -h "$ESP_B_BROKER" -p "$ESP_MQTT_PORT" -t "$topic_ab" >"$recv_ab" 2>&1 &
SUB_PID=$!
sleep "$SUB_PROP_SEC"
publish_until_match "$ESP_A_BROKER" "$ESP_MQTT_PORT" "$topic_ab" "$msg_ab" "$recv_ab" &&
    pass "ESP B receives publish sent to ESP A through Linux" ||
    fail "ESP B did not receive ESP A publish through Linux"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

topic_ba="$TOPIC/b-to-a"
msg_ba="esp-b-linux-esp-a-$(date +%s)"
recv_ba="$OUT_DIR/recv-a.log"
rm -f "$recv_ba"
"$CLI" sub -h "$ESP_A_BROKER" -p "$ESP_MQTT_PORT" -t "$topic_ba" >"$recv_ba" 2>&1 &
SUB_PID=$!
sleep "$SUB_PROP_SEC"
publish_until_match "$ESP_B_BROKER" "$ESP_MQTT_PORT" "$topic_ba" "$msg_ba" "$recv_ba" &&
    pass "ESP A receives publish sent to ESP B through Linux" ||
    fail "ESP A did not receive ESP B publish through Linux"
kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""

kill -0 "$LINUX_PID" >/dev/null 2>&1 && pass "Linux middle broker survived" || fail "Linux middle broker exited"

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
