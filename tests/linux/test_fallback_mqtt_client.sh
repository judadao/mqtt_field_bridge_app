#!/usr/bin/env bash
# Validate a client-side MQTT fallback policy:
#   1. Try HOST:broker_port first.
#   2. If primary is down/refused, retry HOST:fallback_port.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/fallback_client"
CLI="$BROKER_DIR/build_out/mqtt_cli"
FALLBACK_CLIENT="$ROOT_DIR/tests/linux/fallback_mqtt_client.sh"
DUAL_BROKER_A="$OUT/fallback_listener_broker_a"
DUAL_BROKER_B="$OUT/fallback_listener_broker_b"

PRIMARY_PORT=${PRIMARY_PORT:-12883}
FALLBACK_PORT=${FALLBACK_PORT:-12884}
MESH_B_PORT=${MESH_B_PORT:-12885}
P2P_A_PORT=${P2P_A_PORT:-15884}
P2P_B_PORT=${P2P_B_PORT:-15885}
DISC_PORT=${DISC_PORT:-15886}
TOPIC=${TOPIC:-site/field-a/fallback-client}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-8}

PASS=0
FAIL=0
PRIMARY_PID=""
FALLBACK_PID=""
MESH_B_PID=""
SUB_PID=""
PRIMARY_SUB_PID=""
WRAPPER_SUB_PID=""

ok() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1" >&2; }

cleanup() {
    for pid in "$SUB_PID" "$PRIMARY_SUB_PID" "$WRAPPER_SUB_PID" "$PRIMARY_PID" "$FALLBACK_PID" "$MESH_B_PID"; do
        [ -n "$pid" ] && kill "$pid" >/dev/null 2>&1 || true
    done
    wait >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

wait_for_tcp() {
    host=$1
    port=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        timeout 1 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    return 1
}

wait_for_match() {
    file=$1
    pattern=$2
    timeout_sec=$3
    deadline=$((SECONDS + timeout_sec))
    while [ "$SECONDS" -lt "$deadline" ]; do
        grep -Fq "$pattern" "$file" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}

build_broker() {
    name=$1
    port=$2
    out="$OUT/broker_$name"

    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DMQTT_BROKER_PORT="$port" \
        -I"$BROKER_DIR/include" -I"$BROKER_DIR" \
        "$BROKER_DIR/src/broker.c" "$BROKER_DIR/src/client.c" \
        "$BROKER_DIR/src/main.c" "$BROKER_DIR/src/packet.c" \
        "$BROKER_DIR/src/session.c" "$BROKER_DIR/src/topic.c" \
        "$BROKER_DIR/platform/posix/platform_posix.c" \
        -o "$out" -lpthread
}

build_dual_broker() {
    out=$1
    p2p_port=$2

    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DCONFIG_MQTT_P2P_DYNAMIC \
        -DCONFIG_MQTT_P2P_STATIC_SEEDS_ONLY \
        -DP2P_PORT="$p2p_port" \
        -DP2P_DISCOVERY_PORT="$DISC_PORT" \
        -I"$BROKER_DIR/include" -I"$BROKER_DIR" \
        "$ROOT_DIR/tests/linux/fallback_listener_broker.c" \
        "$BROKER_DIR/src/broker.c" "$BROKER_DIR/src/client.c" \
        "$BROKER_DIR/src/packet.c" "$BROKER_DIR/src/session.c" \
        "$BROKER_DIR/src/topic.c" \
        "$BROKER_DIR/src/p2p_discover.c" "$BROKER_DIR/src/p2p_election.c" \
        "$BROKER_DIR/src/p2p_peer.c" "$BROKER_DIR/src/p2p_router.c" \
        "$BROKER_DIR/src/p2p_shard.c" \
        "$BROKER_DIR/platform/posix/platform_posix.c" \
        -o "$out" -lpthread
}

mkdir -p "$OUT"

if [ ! -x "$CLI" ]; then
    make -C "$BROKER_DIR" -f Makefile.linux all >/dev/null
fi

build_broker primary "$PRIMARY_PORT"
build_broker fallback "$FALLBACK_PORT"
build_dual_broker "$DUAL_BROKER_A" "$P2P_A_PORT"
build_dual_broker "$DUAL_BROKER_B" "$P2P_B_PORT"

echo "=== test_fallback_mqtt_client.sh ==="
echo "primary=127.0.0.1:$PRIMARY_PORT fallback=127.0.0.1:$FALLBACK_PORT"

fuser -k "$PRIMARY_PORT/tcp" "$FALLBACK_PORT/tcp" >/dev/null 2>&1 || true
sleep 0.2

"$OUT/broker_fallback" >"$OUT/fallback.log" 2>&1 &
FALLBACK_PID=$!
wait_for_tcp 127.0.0.1 "$FALLBACK_PORT" "$WAIT_MSG_SEC" \
    && ok "fallback broker TCP open" \
    || fail "fallback broker TCP closed"

"$CLI" sub -h 127.0.0.1 -p "$FALLBACK_PORT" -i fallback_probe -t "$TOPIC" \
    -q 2 \
    >"$OUT/fallback-sub.out" 2>"$OUT/fallback-sub.err" &
SUB_PID=$!
wait_for_match "$OUT/fallback-sub.err" "subscribed" "$WAIT_MSG_SEC" \
    && ok "fallback subscriber connected" \
    || fail "fallback subscriber did not connect"

"$FALLBACK_CLIENT" sub \
    --host 127.0.0.1 \
    --broker-port "$PRIMARY_PORT" \
    --fallback-port "$FALLBACK_PORT" \
    --id wrapper_sub \
    --topic "$TOPIC" \
    --qos 2 \
    >"$OUT/wrapper-sub.out" 2>"$OUT/wrapper-sub.err" &
WRAPPER_SUB_PID=$!
wait_for_match "$OUT/wrapper-sub.err" "subscribed" "$WAIT_MSG_SEC" \
    && ok "client subscribe falls back when primary is down" \
    || fail "client subscribe did not fall back when primary is down"

sub_msg="fallback-sub-$(date +%s)"
"$CLI" pub -h 127.0.0.1 -p "$FALLBACK_PORT" -i direct_fallback_pub \
    -t "$TOPIC" -m "$sub_msg" -q 2 >/dev/null
wait_for_match "$OUT/wrapper-sub.out" "$sub_msg" "$WAIT_MSG_SEC" \
    && ok "fallback subscriber wrapper receives publish" \
    || fail "fallback subscriber wrapper missed publish"

kill "$WRAPPER_SUB_PID" >/dev/null 2>&1 || true
wait "$WRAPPER_SUB_PID" >/dev/null 2>&1 || true
WRAPPER_SUB_PID=""

msg="fallback-when-primary-down-$(date +%s)"
if "$FALLBACK_CLIENT" pub \
    --host 127.0.0.1 \
    --broker-port "$PRIMARY_PORT" \
    --fallback-port "$FALLBACK_PORT" \
    --id fallback_pub \
    --topic "$TOPIC" \
    --message "$msg" \
    --qos 2 \
    >"$OUT/client-primary-down.out" 2>"$OUT/client-primary-down.err"; then
    ok "client publish succeeds when primary is down"
else
    fail "client publish failed when primary is down"
fi

grep -Fq "selected=fallback" "$OUT/client-primary-down.err" \
    && ok "client selected fallback port" \
    || fail "client did not report fallback selection"
wait_for_match "$OUT/fallback-sub.out" "$msg" "$WAIT_MSG_SEC" \
    && ok "fallback broker received primary-down publish" \
    || fail "fallback broker did not receive primary-down publish"

"$OUT/broker_primary" >"$OUT/primary.log" 2>&1 &
PRIMARY_PID=$!
wait_for_tcp 127.0.0.1 "$PRIMARY_PORT" "$WAIT_MSG_SEC" \
    && ok "primary broker TCP open" \
    || fail "primary broker TCP closed"

"$CLI" sub -h 127.0.0.1 -p "$PRIMARY_PORT" -i primary_probe -t "$TOPIC" \
    -q 2 \
    >"$OUT/primary-sub.out" 2>"$OUT/primary-sub.err" &
PRIMARY_SUB_PID=$!
wait_for_match "$OUT/primary-sub.err" "subscribed" "$WAIT_MSG_SEC" \
    && ok "primary subscriber connected" \
    || fail "primary subscriber did not connect"

msg2="primary-when-available-$(date +%s)"
if "$FALLBACK_CLIENT" pub \
    --host 127.0.0.1 \
    --broker-port "$PRIMARY_PORT" \
    --fallback-port "$FALLBACK_PORT" \
    --id primary_pub \
    --topic "$TOPIC" \
    --message "$msg2" \
    --qos 2 \
    >"$OUT/client-primary-up.out" 2>"$OUT/client-primary-up.err"; then
    ok "client publish succeeds when primary is up"
else
    fail "client publish failed when primary is up"
fi

grep -Fq "selected=primary" "$OUT/client-primary-up.err" \
    && ok "client selected primary port" \
    || fail "client did not report primary selection"
wait_for_match "$OUT/primary-sub.out" "$msg2" "$WAIT_MSG_SEC" \
    && ok "primary broker received primary-up publish" \
    || fail "primary broker did not receive primary-up publish"

kill "$PRIMARY_SUB_PID" >/dev/null 2>&1 || true
wait "$PRIMARY_SUB_PID" >/dev/null 2>&1 || true
PRIMARY_SUB_PID=""

kill "$PRIMARY_PID" "$FALLBACK_PID" "$SUB_PID" >/dev/null 2>&1 || true
wait "$PRIMARY_PID" "$FALLBACK_PID" "$SUB_PID" >/dev/null 2>&1 || true
PRIMARY_PID=""
FALLBACK_PID=""
SUB_PID=""

fuser -k "$PRIMARY_PORT/tcp" "$FALLBACK_PORT/tcp" "$MESH_B_PORT/tcp" \
    "$P2P_A_PORT/tcp" "$P2P_B_PORT/tcp" >/dev/null 2>&1 || true
sleep 0.2
MQTT_P2P_PEERS="127.0.0.1:$P2P_B_PORT" \
    "$DUAL_BROKER_A" "$PRIMARY_PORT" "$FALLBACK_PORT" \
    >"$OUT/dual-listener-a.log" 2>&1 &
PRIMARY_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$P2P_A_PORT" \
    "$DUAL_BROKER_B" "$MESH_B_PORT" "$((FALLBACK_PORT + 100))" \
    >"$OUT/dual-listener-b.log" 2>&1 &
MESH_B_PID=$!
wait_for_tcp 127.0.0.1 "$PRIMARY_PORT" "$WAIT_MSG_SEC" \
    && wait_for_tcp 127.0.0.1 "$FALLBACK_PORT" "$WAIT_MSG_SEC" \
    && wait_for_tcp 127.0.0.1 "$MESH_B_PORT" "$WAIT_MSG_SEC" \
    && ok "mesh brokers expose primary and fallback listeners" \
    || fail "mesh brokers did not expose listeners"

"$CLI" sub -h 127.0.0.1 -p "$FALLBACK_PORT" -i dual_fallback_sub -t "$TOPIC" \
    -q 2 \
    >"$OUT/dual-fallback-sub.out" 2>"$OUT/dual-fallback-sub.err" &
SUB_PID=$!
wait_for_match "$OUT/dual-fallback-sub.err" "subscribed" "$WAIT_MSG_SEC" \
    && ok "fallback port accepts mesh subscriber" \
    || fail "fallback port subscriber failed"

dual_msg="dual-listener-$(date +%s)"
sleep 4
"$CLI" pub -h 127.0.0.1 -p "$MESH_B_PORT" -i dual_primary_pub \
    -t "$TOPIC" -m "$dual_msg" -q 2 >/dev/null
wait_for_match "$OUT/dual-fallback-sub.out" "$dual_msg" "$WAIT_MSG_SEC" \
    && ok "mesh routes remote publish to fallback subscriber" \
    || fail "mesh did not route remote publish to fallback subscriber"

kill "$SUB_PID" >/dev/null 2>&1 || true
wait "$SUB_PID" >/dev/null 2>&1 || true
SUB_PID=""
sleep 4
gone_msg="dual-listener-after-unsub-$(date +%s)"
"$CLI" pub -h 127.0.0.1 -p "$MESH_B_PORT" -i dual_primary_pub2 \
    -t "$TOPIC" -m "$gone_msg" -q 2 >/dev/null
if wait_for_match "$OUT/dual-fallback-sub.out" "$gone_msg" 3; then
    fail "mesh still delivered after fallback subscriber disconnected"
else
    ok "mesh subscription removed after fallback subscriber disconnect"
fi

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
