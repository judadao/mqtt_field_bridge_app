#!/usr/bin/env bash
# stress_throughput.sh — 3-node high-throughput stress test.
#
# 3 brokers in P2P mesh. PUB_COUNT publishers on B1.
# SUB_COUNT subscribers on each of B2, B3 receive.
# Reports aggregate throughput; verifies minimum message count.
#
# Knobs:
#   PUB_COUNT            publishers on B1 (default: 5)
#   SUB_COUNT            subscribers on B2 and B3 (default: 3)
#   DURATION             test duration in seconds (default: 10)
#   SETTLE_SEC           P2P formation wait (default: 5)
#   MIN_THROUGHPUT_MSG   minimum total msgs expected (default: 500)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$ROOT_DIR/tests/linux/out/stress_throughput"
CLI="$BROKER_DIR/build_out/mqtt_cli"

PUB_COUNT=${PUB_COUNT:-5}
SUB_COUNT=${SUB_COUNT:-3}
DURATION=${DURATION:-10}
SETTLE_SEC=${SETTLE_SEC:-5}
MIN_THROUGHPUT_MSG=${MIN_THROUGHPUT_MSG:-500}
DISC_PORT=16850

PASS=0; FAIL=0
B1_PID="" B2_PID="" B3_PID=""
ALL_PIDS=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID $B3_PID $ALL_PIDS; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"

stop_workload() {
    for pid in $ALL_PIDS; do
        kill "$pid" 2>/dev/null || true
    done

    sleep 0.5

    for pid in $ALL_PIDS; do
        kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null || true
    done

    for pid in $ALL_PIDS; do
        wait "$pid" 2>/dev/null || true
    done
}

_build() {
    name=$1; mqtt_port=$2; p2p_port=$3
    out="$OUT/broker_${name}"
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

B1_MQTT=13883; B1_P2P=16884
B2_MQTT=13884; B2_P2P=16886
B3_MQTT=13885; B3_P2P=16888

_build b1 $B1_MQTT $B1_P2P
_build b2 $B2_MQTT $B2_P2P
_build b3 $B3_MQTT $B3_P2P

echo "=== stress_throughput.sh (${PUB_COUNT} pubs, ${SUB_COUNT} subs/broker, ${DURATION}s) ==="

fuser -k ${B1_MQTT}/tcp ${B2_MQTT}/tcp ${B3_MQTT}/tcp \
         ${B1_P2P}/tcp  ${B2_P2P}/tcp  ${B3_P2P}/tcp 2>/dev/null || true
sleep 0.3

MQTT_P2P_PEERS="127.0.0.1:$B2_P2P,127.0.0.1:$B3_P2P" \
    "$OUT/broker_b1" >/tmp/tp_b1.log 2>&1 & B1_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$OUT/broker_b2" >/tmp/tp_b2.log 2>&1 & B2_PID=$!
MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B2_P2P" \
    "$OUT/broker_b3" >/tmp/tp_b3.log 2>&1 & B3_PID=$!
sleep "$SETTLE_SEC"

# start subscribers
SUB_FILES=""
j=0
while [ $j -lt "$SUB_COUNT" ]; do
    f="$OUT/sub_b2_${j}.out"; : >"$f"; SUB_FILES="$SUB_FILES $f"
    "$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/tp/4510/#" \
        >"$f" 2>"$OUT/sub_b2_${j}.err" & ALL_PIDS="$ALL_PIDS $!"
    f="$OUT/sub_b3_${j}.out"; : >"$f"; SUB_FILES="$SUB_FILES $f"
    "$CLI" sub -h 127.0.0.1 -p "$B3_MQTT" -t "site/tp/4510/#" \
        >"$f" 2>"$OUT/sub_b3_${j}.err" & ALL_PIDS="$ALL_PIDS $!"
    j=$((j+1))
done
sleep 0.3

# start publishers on B1
j=0
while [ $j -lt "$PUB_COUNT" ]; do
    (end=$(($(date +%s) + DURATION))
     while [ "$(date +%s)" -lt "$end" ]; do
         "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
             -t "site/tp/4510/io" -m "tp" >/dev/null 2>&1 || true
     done) & ALL_PIDS="$ALL_PIDS $!"
    j=$((j+1))
done

printf '  Publishing for %ds...\n' "$DURATION"
sleep "$DURATION"

# stop publishers and subscribers, but keep brokers alive for survival checks
stop_workload

total=0
for f in $SUB_FILES; do
    n=$(wc -l <"$f" 2>/dev/null || echo 0)
    total=$((total + n))
done
per_sec=$((total / DURATION))
printf '  Total: %d msgs received (%d msg/s)\n' "$total" "$per_sec"

[ "$total" -ge "$MIN_THROUGHPUT_MSG" ] \
    && ok "throughput: $total >= $MIN_THROUGHPUT_MSG minimum" \
    || fail "throughput: $total < $MIN_THROUGHPUT_MSG minimum"

for pid in $B1_PID $B2_PID $B3_PID; do
    kill -0 "$pid" 2>/dev/null \
        && ok "broker $pid survived" \
        || fail "broker $pid crashed"
done

echo ""; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
