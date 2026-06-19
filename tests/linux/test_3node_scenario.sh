#!/usr/bin/env bash
# test_3node_scenario.sh — integration test for the generic 3-node field scenario.
#
# Builds three separate broker binaries (compile-time ports), simulates a
# chain topology on localhost (B1 <-> B2 <-> B3), and verifies that bridged
# nodes behave as one broker network.
#
# Knobs:
#   SETTLE_SEC   P2P mesh formation wait (default: 8)
#   WAIT_MSG_SEC max seconds to wait for an expected message (default: 8)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/3node"
CLI="$BROKER_DIR/build_out/mqtt_cli"

SETTLE_SEC=${SETTLE_SEC:-8}
WAIT_MSG_SEC=${WAIT_MSG_SEC:-8}
DISC_PORT=14850

PASS=0; FAIL=0
B1_PID="" B2_PID="" B3_PID="" SUB_PID=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID $B3_PID $SUB_PID; do
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

_needs_rebuild() {
    tgt=$1
    [ ! -x "$tgt" ] && return 0
    find "$BROKER_DIR/src" "$BROKER_DIR/include" "$BROKER_DIR/platform" \
        -type f \( -name '*.c' -o -name '*.h' \) -newer "$tgt" \
        | grep -q .
}

_build() {
    name=$1; mqtt_port=$2; p2p_port=$3
    tgt="$OUT/broker_${name}"
    if ! _needs_rebuild "$tgt"; then
        return 0
    fi
    printf '  Building broker_%s...\n' "$name"
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
        -o "$tgt" -lpthread
}

[ -x "$CLI" ] || make -C "$BROKER_DIR" -f Makefile.linux all >/dev/null 2>&1

B1_MQTT=11883; B1_P2P=14884
B2_MQTT=11884; B2_P2P=14886
B3_MQTT=11885; B3_P2P=14888

_build b1 $B1_MQTT $B1_P2P
_build b2 $B2_MQTT $B2_P2P
_build b3 $B3_MQTT $B3_P2P

echo "=== test_3node_scenario.sh ==="

# clear any leftover processes from previous runs on these ports
fuser -k ${B1_MQTT}/tcp ${B2_MQTT}/tcp ${B3_MQTT}/tcp \
         ${B1_P2P}/tcp  ${B2_P2P}/tcp  ${B3_P2P}/tcp >/dev/null 2>&1 || true
sleep 0.3

MQTT_P2P_PEERS="127.0.0.1:$B2_P2P" \
    "$OUT/broker_b1" >/tmp/3n_b1.log 2>&1 & B1_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$OUT/broker_b2" >/tmp/3n_b2.log 2>&1 & B2_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$B2_P2P" \
    "$OUT/broker_b3" >/tmp/3n_b3.log 2>&1 & B3_PID=$!

printf '  Waiting %ds for P2P mesh...\n' "$SETTLE_SEC"
sleep "$SETTLE_SEC"

# ── T1: Node2 subscriber receives Node1 publish ──────────────────────────
"$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/field-a/data/#" \
    >"$OUT/t1_recv.out" 2>"$OUT/t1_sub.err" & SUB_PID=$!
wait_for_match "$OUT/t1_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep 1.5   # allow subscription to propagate B2→B1 via P2P
"$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/data/io" -m "t1-ok" >/dev/null 2>&1
wait_for_match "$OUT/t1_recv.out" "t1-ok" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "t1-ok" "$OUT/t1_recv.out" \
    && ok "T1: Node2 receives Node1 publish" \
    || fail "T1: Node2 did not receive Node1 publish (got: $(cat "$OUT/t1_recv.out"))"

# ── T2: Node3 subscriber receives Node1 publish through Node2 ─────────────
"$CLI" sub -h 127.0.0.1 -p "$B3_MQTT" -t "site/field-a/data/#" \
    >"$OUT/t2_recv.out" 2>"$OUT/t2_sub.err" & SUB_PID=$!
wait_for_match "$OUT/t2_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep 1.5
"$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/data/io" -m "t2-ok" >/dev/null 2>&1
wait_for_match "$OUT/t2_recv.out" "t2-ok" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "t2-ok" "$OUT/t2_recv.out" \
    && ok "T2: Node3 receives Node1 publish through Node2" \
    || fail "T2: Node3 did not receive Node1 publish through Node2"

# ── T3: Node1 local delivery when Node2 is offline ───────────────────────
kill "$B2_PID" 2>/dev/null || true; wait "$B2_PID" 2>/dev/null || true; B2_PID=""
sleep 0.5
"$CLI" sub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/data/#" \
    >"$OUT/t3_recv.out" 2>"$OUT/t3_sub.err" & SUB_PID=$!
wait_for_match "$OUT/t3_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep 0.3
"$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/data/io" -m "t3-local" >/dev/null 2>&1
wait_for_match "$OUT/t3_recv.out" "t3-local" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "t3-local" "$OUT/t3_recv.out" \
    && ok "T3: Node1 local delivery works when Node2 offline" \
    || fail "T3: Node1 local delivery failed"

# ── T4: Node2 recovers and receives after restart ─────────────────────────
MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$OUT/broker_b2" >/tmp/3n_b2r.log 2>&1 & B2_PID=$!
sleep "$SETTLE_SEC"
"$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/field-a/data/#" \
    >"$OUT/t4_recv.out" 2>"$OUT/t4_sub.err" & SUB_PID=$!
wait_for_match "$OUT/t4_sub.err" "subscribed" "$WAIT_MSG_SEC" || true
sleep 1.5
"$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/data/io" -m "t4-restart" >/dev/null 2>&1
wait_for_match "$OUT/t4_recv.out" "t4-restart" "$WAIT_MSG_SEC" || true
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true; SUB_PID=""
grep -q "t4-restart" "$OUT/t4_recv.out" \
    && ok "T4: Node2 receives after restart" \
    || fail "T4: Node2 failed to receive after restart"

echo ""; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
