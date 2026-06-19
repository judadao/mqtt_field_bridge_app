#!/usr/bin/env bash
# stress_reconnect.sh — reconnect stress test.
#
# Publisher on B1 at ~200 msg/s. B2 is killed and restarted RESTART_COUNT times.
# After each restart, B2 must re-subscribe and receive messages. B1 must not hang.
#
# Knobs:
#   RESTART_COUNT  number of kill+restart cycles (default: 5)
#   SETTLE_SEC     P2P re-mesh wait after restart (default: 5)
#   VERIFY_TIMEOUT_SEC  max seconds to wait for post-restart messages (default: 5)
#   PUB_INTERVAL_SEC publisher interval while B2 reconnects (default: 0.02)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/stress_reconnect"
CLI="$BROKER_DIR/build_out/mqtt_cli"

RESTART_COUNT=${RESTART_COUNT:-5}
SETTLE_SEC=${SETTLE_SEC:-5}
VERIFY_MSGS=3
VERIFY_TIMEOUT_SEC=${VERIFY_TIMEOUT_SEC:-5}
PUB_INTERVAL_SEC=${PUB_INTERVAL_SEC:-0.02}
DISC_PORT=15850

PASS=0; FAIL=0
B1_PID="" B2_PID="" PUB_PID="" SUB_PID=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID $PUB_PID $SUB_PID; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"

_needs_rebuild() {
    out=$1
    [ ! -x "$out" ] && return 0
    find "$BROKER_DIR/src" "$BROKER_DIR/include" "$BROKER_DIR/platform" \
        -type f \( -name '*.c' -o -name '*.h' \) -newer "$out" \
        | grep -q .
}

wait_for_lines() {
    file=$1
    want=$2
    timeout=$3
    start=$(date +%s)

    while :; do
        n=$(wc -l <"$file" 2>/dev/null || echo 0)
        [ "$n" -ge "$want" ] && return 0
        now=$(date +%s)
        [ $((now - start)) -ge "$timeout" ] && return 1
        sleep 0.1
    done
}

_build() {
    name=$1; mqtt_port=$2; p2p_port=$3
    out="$OUT/broker_${name}"
    if ! _needs_rebuild "$out"; then
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
        -o "$out" -lpthread
}

[ -x "$CLI" ] || make -C "$BROKER_DIR" -f Makefile.linux all >/dev/null 2>&1

B1_MQTT=12883; B1_P2P=15884
B2_MQTT=12884; B2_P2P=15886

_build b1 $B1_MQTT $B1_P2P
_build b2 $B2_MQTT $B2_P2P

echo "=== stress_reconnect.sh (${RESTART_COUNT} cycles) ==="

fuser -k ${B1_MQTT}/tcp ${B2_MQTT}/tcp ${B1_P2P}/tcp ${B2_P2P}/tcp >/dev/null 2>&1 || true
sleep 0.3

MQTT_P2P_PEERS="127.0.0.1:$B2_P2P" "$OUT/broker_b1" >/tmp/sr_b1.log 2>&1 & B1_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$B1_P2P" "$OUT/broker_b2" >/tmp/sr_b2.log 2>&1 & B2_PID=$!
sleep "$SETTLE_SEC"

# background publisher on B1
(while true; do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" -t "site/stress/data/io" -m "s" >/dev/null 2>&1 || true
    sleep "$PUB_INTERVAL_SEC"
done) & PUB_PID=$!

for i in $(seq 1 "$RESTART_COUNT"); do
    printf '  cycle %d/%d\n' "$i" "$RESTART_COUNT"
    kill "$B2_PID" 2>/dev/null || true; wait "$B2_PID" 2>/dev/null || true
    kill -0 "$B1_PID" 2>/dev/null || { fail "B1 crashed at cycle $i"; continue; }
    sleep 0.2
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P" \
        "$OUT/broker_b2" >/tmp/sr_b2_${i}.log 2>&1 & B2_PID=$!
    sleep "$SETTLE_SEC"

    RECV="$OUT/cycle_${i}_recv.out"
    : >"$RECV"
    "$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/stress/data/#" \
        >"$RECV" 2>"$OUT/cycle_${i}_sub.err" & SUB_PID=$!
    wait_for_lines "$RECV" "$VERIFY_MSGS" "$VERIFY_TIMEOUT_SEC" || true
    kill "$SUB_PID" 2>/dev/null || true
    wait "$SUB_PID" 2>/dev/null || true
    n=$(wc -l <"$RECV" || echo 0)
    [ "$n" -ge "$VERIFY_MSGS" ] \
        && ok "cycle $i: B2 received $n msgs after restart" \
        || fail "cycle $i: B2 only $n/$VERIFY_MSGS msgs"
done

kill -0 "$B1_PID" 2>/dev/null \
    && ok "B1 survived all $RESTART_COUNT cycles" \
    || fail "B1 crashed during stress"

echo ""; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
