#!/usr/bin/env bash
# test_chain_scale.sh — scalable chain-topology broker validation.
#
# Default topology: B1 <-> B2 <-> ... <-> B10. A subscriber on the last node
# must receive a publish from the first node, proving the connected bridge graph
# behaves as one broker network without requiring a full mesh.
#
# Knobs:
#   NODE_COUNT   number of brokers in the chain (default: 10)
#   SETTLE_SEC   P2P formation wait (default: 12)
#   WAIT_MSG_SEC max seconds to wait for the expected message (default: 10)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/chain_scale"
CLI="$BROKER_DIR/build_out/mqtt_cli"

NODE_COUNT=${NODE_COUNT:-10}
SETTLE_SEC=${SETTLE_SEC:-12}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-10}
DISC_PORT=18850
MQTT_BASE=21883
P2P_BASE=24884

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

needs_rebuild() {
    out=$1
    [ ! -x "$out" ] && return 0
    find "$BROKER_DIR/src" "$BROKER_DIR/include" "$BROKER_DIR/platform" \
        -type f \( -name '*.c' -o -name '*.h' \) -newer "$out" \
        | grep -q .
}

build_broker() {
    idx=$1
    mqtt_port=$((MQTT_BASE + idx))
    p2p_port=$((P2P_BASE + (idx * 2)))
    out="$OUT/broker_${idx}"
    if ! needs_rebuild "$out"; then
        return 0
    fi
    printf '  Building broker_%d...\n' "$idx"
    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DCONFIG_MQTT_P2P_DYNAMIC \
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
    if [ "$idx" -gt 0 ]; then
        prev=$((idx - 1))
        peers="127.0.0.1:$((P2P_BASE + (prev * 2)))"
    fi
    if [ "$idx" -lt $((NODE_COUNT - 1)) ]; then
        next=$((idx + 1))
        if [ -n "$peers" ]; then peers="$peers,"; fi
        peers="${peers}127.0.0.1:$((P2P_BASE + (next * 2)))"
    fi
    printf '%s\n' "$peers"
}

[ "$NODE_COUNT" -ge 2 ] || { echo "NODE_COUNT must be >= 2" >&2; exit 1; }
[ -x "$CLI" ] || make -C "$BROKER_DIR" -f Makefile.linux all >/dev/null 2>&1

echo "=== test_chain_scale.sh (${NODE_COUNT} node chain) ==="

ports=""
for i in $(seq 0 $((NODE_COUNT - 1))); do
    ports="$ports $((MQTT_BASE + i))/tcp $((P2P_BASE + (i * 2)))/tcp"
    build_broker "$i"
done
fuser -k $ports >/dev/null 2>&1 || true
sleep 0.3

for i in $(seq 0 $((NODE_COUNT - 1))); do
    peers=$(peer_list_for "$i")
    MQTT_P2P_PEERS="$peers" "$OUT/broker_${i}" >"$OUT/broker_${i}.log" 2>&1 &
    PIDS="$PIDS $!"
done

printf '  Waiting %ds for chain to converge...\n' "$SETTLE_SEC"
sleep "$SETTLE_SEC"

last=$((NODE_COUNT - 1))
"$CLI" sub -h 127.0.0.1 -p "$((MQTT_BASE + last))" -t "site/scale/data/#" \
    >"$OUT/recv.out" 2>"$OUT/sub.err" & SUB_PID=$!
wait_for_match "$OUT/sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep 1.5

"$CLI" pub -h 127.0.0.1 -p "$MQTT_BASE" \
    -t "site/scale/data/io" -m "chain-ok" >/dev/null 2>&1
wait_for_match "$OUT/recv.out" "chain-ok" "$WAIT_MSG_SEC" || true

grep -q "chain-ok" "$OUT/recv.out" \
    && ok "B$((last + 1)) receives B1 publish through chain" \
    || fail "B$((last + 1)) did not receive B1 publish"

alive=0
for pid in $PIDS; do
    kill -0 "$pid" 2>/dev/null && alive=$((alive + 1)) || true
done
[ "$alive" -eq "$NODE_COUNT" ] \
    && ok "all $NODE_COUNT brokers survived" \
    || fail "only $alive/$NODE_COUNT brokers survived"

echo ""; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
