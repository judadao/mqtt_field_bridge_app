#!/usr/bin/env bash
# test_esp32_linux_chain_bridge.sh — hardware bridge test:
#   ESP32 broker <-> Linux broker5 <-> broker4 <-> broker3 <-> broker2 <-> broker1
#
# Verifies:
#   1. A Linux subscriber on broker1 receives a publish sent to the ESP32 broker.
#   2. An ESP32 subscriber receives a publish sent to Linux broker1.
#
# Knobs:
#   ESP32_HOST       ESP32 HTTP/MQTT host (default: 192.168.127.4)
#   LINUX_PEER_HOST  Linux host/IP visible from ESP32 (default: 192.168.127.5)
#   SETTLE_SEC       P2P formation wait (default: 14)
#   WAIT_MSG_SEC     max seconds to wait for expected message (default: 14)
#   SUB_PROP_SEC     remote subscription propagation wait (default: 4)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/esp32_linux_chain"
CLI="$BROKER_DIR/build_out/mqtt_cli"

ESP32_HOST=${ESP32_HOST:-192.168.127.4}
ESP32_MQTT=${ESP32_MQTT:-1883}
ESP32_P2P=${ESP32_P2P:-4884}
LINUX_PEER_HOST=${LINUX_PEER_HOST:-192.168.127.5}
SETTLE_SEC=${SETTLE_SEC:-14}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-14}
SUB_PROP_SEC=${SUB_PROP_SEC:-4}
DISC_PORT=19850
MQTT_BASE=31883
P2P_BASE=34884
NODE_COUNT=5

PASS=0; FAIL=0
PIDS=""
SUB_PID=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $PIDS $SUB_PID; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"

wait_for_match() {
    file=$1
    pattern=$2
    timeout=$3
    start=$(date +%s)

    while :; do
        grep -q "$pattern" "$file" 2>/dev/null && return 0
        now=$(date +%s)
        [ $((now - start)) -ge "$timeout" ] && return 1
        sleep 0.1
    done
}

wait_for_http() {
    timeout=$1
    start=$(date +%s)

    while :; do
        curl -fsS --max-time 2 "http://$ESP32_HOST:8080/status" >/dev/null 2>&1 && return 0
        now=$(date +%s)
        [ $((now - start)) -ge "$timeout" ] && return 1
        sleep 0.5
    done
}

publish_until_match() {
    host=$1
    port=$2
    topic=$3
    payload=$4
    file=$5
    timeout=$6
    start=$(date +%s)

    while :; do
        "$CLI" pub -h "$host" -p "$port" -t "$topic" -m "$payload" >/dev/null 2>&1 || true
        wait_for_match "$file" "$payload" 1 && return 0
        now=$(date +%s)
        [ $((now - start)) -ge "$timeout" ] && return 1
        sleep 0.5
    done
}

needs_rebuild() {
    out=$1
    [ ! -x "$out" ] && return 0
    find "$BROKER_DIR/src" "$BROKER_DIR/include" "$BROKER_DIR/platform" \
        -type f \( -name '*.c' -o -name '*.h' \) -newer "$out" \
        | grep -q .
}

build_broker() {
    idx=$1
    mqtt_port=$((MQTT_BASE + idx - 1))
    p2p_port=$((P2P_BASE + ((idx - 1) * 2)))
    out="$OUT/broker_${idx}"
    if ! needs_rebuild "$out"; then
        return 0
    fi
    printf '  Building broker_%d...\n' "$idx"
    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DCONFIG_MQTT_P2P_DYNAMIC \
        -DCONFIG_MQTT_P2P_STATIC_SEEDS_ONLY \
        -DMQTT_BROKER_PORT="$mqtt_port" \
        -DP2P_PORT="$p2p_port" \
        -DP2P_DISCOVERY_PORT="$DISC_PORT" \
        -I"$BROKER_DIR/include" -I"$BROKER_DIR" \
        "$BROKER_DIR/src/broker.c" "$BROKER_DIR/src/client.c" \
        "$BROKER_DIR/src/main.c"   "$BROKER_DIR/src/packet.c" \
        "$BROKER_DIR/src/session.c" "$BROKER_DIR/src/topic.c" \
        "$BROKER_DIR/src/p2p_discover.c" "$BROKER_DIR/src/p2p_election.c" \
        "$BROKER_DIR/src/p2p_peer.c"     "$BROKER_DIR/src/p2p_router.c" \
        "$BROKER_DIR/src/p2p_shard.c" \
        "$BROKER_DIR/platform/posix/platform_posix.c" \
        -o "$out" -lpthread
}

peer_list_for() {
    idx=$1
    peers=""
    if [ "$idx" -gt 1 ]; then
        prev=$((idx - 2))
        peers="127.0.0.1:$((P2P_BASE + (prev * 2)))"
    fi
    if [ "$idx" -lt "$NODE_COUNT" ]; then
        next=$idx
        if [ -n "$peers" ]; then peers="$peers,"; fi
        peers="${peers}127.0.0.1:$((P2P_BASE + (next * 2)))"
    fi
    printf '%s\n' "$peers"
}

configure_esp32_peer() {
    python3 - "$ESP32_HOST" "$LINUX_PEER_HOST" "$((MQTT_BASE + 4))" "$((P2P_BASE + 8))" <<'PY'
import json
import sys
import urllib.request

host, linux_peer_host, mqtt_port, p2p_port = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
base = f"http://{host}:8080"

def req(path, method="GET", body=None):
    data = None if body is None else json.dumps(body).encode()
    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/json"
    with urllib.request.urlopen(urllib.request.Request(base + path, data=data, headers=headers, method=method), timeout=5) as r:
        raw = r.read().decode()
    return json.loads(raw) if raw else {}

cfg = req("/config")
cfg["bridge_enabled"] = 1
cfg["mesh_enabled"] = 1
cfg["broker_enabled"] = 1
req("/config", "POST", cfg)
req("/peers/0", "POST", {
    "name": "linux-broker5",
    "host": linux_peer_host,
    "mqtt_port": mqtt_port,
    "p2p_port": p2p_port,
    "enabled": 1,
})
req("/broker/control", "POST", {"enabled": 1})
print(json.dumps({"esp32": host, "peer_host": linux_peer_host, "peer_mqtt": mqtt_port, "peer_p2p": p2p_port}))
PY
}

[ -x "$CLI" ] || make -C "$BROKER_DIR" -f Makefile.linux all >/dev/null 2>&1

echo "=== test_esp32_linux_chain_bridge.sh ==="
echo "  ESP32 broker:       $ESP32_HOST:$ESP32_MQTT p2p=$ESP32_P2P"
echo "  Linux peer address: $LINUX_PEER_HOST"

wait_for_http "$WAIT_MSG_SEC" || {
    echo "error: ESP32 HTTP not reachable at http://$ESP32_HOST:8080/status" >&2
    exit 1
}

ports=""
for i in $(seq 1 "$NODE_COUNT"); do
    ports="$ports $((MQTT_BASE + i - 1))/tcp $((P2P_BASE + ((i - 1) * 2)))/tcp"
    build_broker "$i"
done
fuser -k $ports >/dev/null 2>&1 || true
sleep 0.3

for i in $(seq 1 "$NODE_COUNT"); do
    peers=$(peer_list_for "$i")
    MQTT_P2P_PEERS="$peers" "$OUT/broker_${i}" >"$OUT/broker_${i}.log" 2>&1 &
    PIDS="$PIDS $!"
done

printf '  Waiting %ds for Linux broker chain 1-2-3-4-5...\n' "$SETTLE_SEC"
sleep "$SETTLE_SEC"

printf '  Configuring ESP32 bridge peer -> Linux broker5...\n'
configure_esp32_peer | tee "$OUT/esp32_config.json"

printf '  Waiting %ds for ESP32 <-> Linux broker5 bridge...\n' "$SETTLE_SEC"
sleep "$SETTLE_SEC"

TOPIC="site/esp32-chain/data/io"

"$CLI" sub -h 127.0.0.1 -p "$MQTT_BASE" -t "$TOPIC" \
    >"$OUT/linux_b1_recv.out" 2>"$OUT/linux_b1_sub.err" & SUB_PID=$!
wait_for_match "$OUT/linux_b1_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep "$SUB_PROP_SEC"
publish_until_match "$ESP32_HOST" "$ESP32_MQTT" "$TOPIC" "esp-to-linux-b1" \
    "$OUT/linux_b1_recv.out" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "esp-to-linux-b1" "$OUT/linux_b1_recv.out" \
    && ok "Linux broker1 receives publish sent to ESP32 broker" \
    || fail "Linux broker1 did not receive ESP32 publish"

"$CLI" sub -h "$ESP32_HOST" -p "$ESP32_MQTT" -t "$TOPIC" \
    >"$OUT/esp32_recv.out" 2>"$OUT/esp32_sub.err" & SUB_PID=$!
wait_for_match "$OUT/esp32_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep "$SUB_PROP_SEC"
publish_until_match "127.0.0.1" "$MQTT_BASE" "$TOPIC" "linux-b1-to-esp" \
    "$OUT/esp32_recv.out" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "linux-b1-to-esp" "$OUT/esp32_recv.out" \
    && ok "ESP32 broker receives publish sent to Linux broker1" \
    || fail "ESP32 broker did not receive Linux broker1 publish"

alive=0
for pid in $PIDS; do
    kill -0 "$pid" 2>/dev/null && alive=$((alive + 1)) || true
done
[ "$alive" -eq "$NODE_COUNT" ] \
    && ok "all $NODE_COUNT Linux brokers survived" \
    || fail "only $alive/$NODE_COUNT Linux brokers survived"

echo ""; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
