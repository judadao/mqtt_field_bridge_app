#!/usr/bin/env bash
# Validate Mode A admission fallback:
#   ESP32 admits only two local MQTT clients, rejects the third, and the third
#   falls back to a Linux broker while bridge routing keeps delivery working.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
OUT_DIR="$ROOT_DIR/tests/linux/out/esp32_linux_admission_fallback"
LINUX_BROKER="$OUT_DIR/linux_fallback_broker"

ESP_HTTP=${ESP_HTTP:-192.168.127.4}
ESP_BROKER=${ESP_BROKER:-192.168.127.15}
ESP_MQTT_PORT=${ESP_MQTT_PORT:-1883}
LINUX_HOST=${LINUX_HOST:-192.168.127.5}
LINUX_MQTT_PORT=${LINUX_MQTT_PORT:-21884}
LINUX_P2P_PORT=${LINUX_P2P_PORT:-24885}
DISC_PORT=${DISC_PORT:-19870}
TOPIC=${TOPIC:-site/field-a/admission-fallback}
SETTLE_SEC=${SETTLE_SEC:-15}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-18}
SUB_PROP_SEC=${SUB_PROP_SEC:-10}

PASS=0
FAIL=0
LINUX_PID=""
SUB_PIDS=()

mkdir -p "$OUT_DIR"

pass() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1" >&2; }

cleanup() {
    for pid in "${SUB_PIDS[@]}"; do
        kill "$pid" >/dev/null 2>&1 || true
    done
    wait "${SUB_PIDS[@]}" >/dev/null 2>&1 || true
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

wait_for_http() {
    deadline=$((SECONDS + WAIT_MSG_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS --max-time 3 "http://$ESP_HTTP:8080/status" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

wait_for_esp_mqtt() {
    deadline=$((SECONDS + WAIT_MSG_SEC))
    while [ "$SECONDS" -lt "$deadline" ]; do
        "$CLI" status -h "$ESP_BROKER" -p "$ESP_MQTT_PORT" >/dev/null 2>&1 && return 0
        sleep 1
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

build_linux_broker() {
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
        -o "$LINUX_BROKER" -lpthread
}

configure_esp_peer() {
    curl -fsS --max-time 6 -X POST -H 'Content-Type: application/json' \
        --data "{\"name\":\"linux-fallback\",\"host\":\"$LINUX_HOST\",\"mqtt_port\":$LINUX_MQTT_PORT,\"enabled\":1}" \
        "http://$ESP_HTTP:8080/peers/0" >"$OUT_DIR/config-peer.json"
}

need curl
need gcc
need rg

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
fi

printf '=== test_esp32_linux_admission_fallback.sh ===\n'
printf 'ESP: http=%s broker=%s mqtt=%s admission=2\n' "$ESP_HTTP" "$ESP_BROKER" "$ESP_MQTT_PORT"
printf 'Linux fallback: host=%s mqtt=%s p2p=%s\n' "$LINUX_HOST" "$LINUX_MQTT_PORT" "$LINUX_P2P_PORT"

wait_for_http && pass "ESP HTTP reachable" || fail "ESP HTTP unreachable"
wait_for_esp_mqtt && pass "ESP broker MQTT reachable" || fail "ESP broker MQTT unreachable"

build_linux_broker
fuser -k "$LINUX_MQTT_PORT/tcp" "$LINUX_P2P_PORT/tcp" >/dev/null 2>&1 || true
"$LINUX_BROKER" >"$OUT_DIR/linux-fallback.log" 2>&1 &
LINUX_PID=$!
wait_for_tcp 127.0.0.1 "$LINUX_MQTT_PORT" "$WAIT_MSG_SEC" &&
    pass "Linux fallback broker TCP open" ||
    fail "Linux fallback broker TCP closed"

configure_esp_peer
sleep "$SETTLE_SEC"

"$CLI" sub -h "$ESP_BROKER" -p "$ESP_MQTT_PORT" -i esp_fill_1 -t "$TOPIC" >"$OUT_DIR/esp-fill-1.log" 2>&1 &
SUB_PIDS+=($!)
"$CLI" sub -h "$ESP_BROKER" -p "$ESP_MQTT_PORT" -i esp_fill_2 -t "$TOPIC" >"$OUT_DIR/esp-fill-2.log" 2>&1 &
SUB_PIDS+=($!)
sleep "$SUB_PROP_SEC"

wait_for_match "$OUT_DIR/esp-fill-1.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "ESP filler client 1 admitted" ||
    fail "ESP filler client 1 not admitted"
wait_for_match "$OUT_DIR/esp-fill-2.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "ESP filler client 2 admitted" ||
    fail "ESP filler client 2 not admitted"

reject_out=$("$CLI" sub -h "$ESP_BROKER" -p "$ESP_MQTT_PORT" -i esp_overflow -t "$TOPIC" 2>&1 >/dev/null || true)
printf '%s\n' "$reject_out" >"$OUT_DIR/esp-overflow.err"
if printf '%s\n' "$reject_out" | rg -qi "server unavailable|code 3"; then
    pass "ESP rejects overflow client with server-unavailable"
else
    fail "ESP overflow client was not rejected as server-unavailable"
fi

"$CLI" sub -h 127.0.0.1 -p "$LINUX_MQTT_PORT" -i fallback_linux -t "$TOPIC" >"$OUT_DIR/linux-fallback-sub.log" 2>&1 &
SUB_PIDS+=($!)
wait_for_match "$OUT_DIR/linux-fallback-sub.log" "subscribed" "$WAIT_MSG_SEC" &&
    pass "overflow client falls back to Linux" ||
    fail "overflow client did not subscribe on Linux"
sleep "$SUB_PROP_SEC"

msg="esp-linux-admission-$(date +%s)"
"$CLI" pub -h 127.0.0.1 -p "$LINUX_MQTT_PORT" -i linux_pub -t "$TOPIC" -m "$msg" >/dev/null 2>&1

wait_for_match "$OUT_DIR/linux-fallback-sub.log" "$msg" "$WAIT_MSG_SEC" &&
    pass "Linux fallback client receives publish" ||
    fail "Linux fallback client missed publish"
wait_for_match "$OUT_DIR/esp-fill-1.log" "$msg" "$WAIT_MSG_SEC" &&
    pass "ESP existing client receives bridged publish" ||
    fail "ESP existing client missed bridged publish"

kill -0 "$LINUX_PID" >/dev/null 2>&1 && pass "Linux fallback broker survived" || fail "Linux fallback broker exited"

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
