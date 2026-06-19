#!/usr/bin/env sh
# stress_throughput.sh — 3-node high-throughput stress test.
#
# 3 brokers in P2P mesh. PUB_COUNT publishers on B1 publish at full speed.
# SUB_COUNT subscribers on each of B2, B3 receive.
# Reports aggregate throughput and verifies no stall.
#
# Knobs:
#   PUB_COUNT   publishers on B1 (default: 5)
#   SUB_COUNT   subscribers on B2 and B3 (default: 3)
#   DURATION    test duration in seconds (default: 10)
#   SETTLE_SEC  P2P formation wait (default: 3)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
BROKER="$BROKER_DIR/build_out/mqtt_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"

PUB_COUNT=${PUB_COUNT:-5}
SUB_COUNT=${SUB_COUNT:-3}
DURATION=${DURATION:-10}
SETTLE_SEC=${SETTLE_SEC:-3}
MIN_THROUGHPUT_MSG=${MIN_THROUGHPUT_MSG:-500}   # minimum total msgs expected across all subs

PASS=0; FAIL=0
B1_PID="" B2_PID="" B3_PID=""
PUB_PIDS="" SUB_PIDS="" SUB_FILES=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID $B3_PID $PUB_PIDS $SUB_PIDS; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    for f in $SUB_FILES; do rm -f "$f"; done
}
trap cleanup EXIT INT TERM

if [ ! -x "$BROKER" ]; then
    printf 'Building broker with P2P...\n'
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all >/dev/null 2>&1
fi

B1_MQTT=13883; B1_P2P=16884; B1_DISC=16885
B2_MQTT=13884; B2_P2P=16886; B2_DISC=16887
B3_MQTT=13885; B3_P2P=16888; B3_DISC=16889

echo "=== stress_throughput.sh (${PUB_COUNT} pubs, ${SUB_COUNT} subs/broker, ${DURATION}s) ==="

MQTT_BROKER_PORT=$B1_MQTT P2P_PORT=$B1_P2P P2P_DISCOVERY_PORT=$B1_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B2_P2P,127.0.0.1:$B3_P2P" \
    "$BROKER" >/tmp/tp_b1.log 2>&1 &
B1_PID=$!
MQTT_BROKER_PORT=$B2_MQTT P2P_PORT=$B2_P2P P2P_DISCOVERY_PORT=$B2_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$BROKER" >/tmp/tp_b2.log 2>&1 &
B2_PID=$!
MQTT_BROKER_PORT=$B3_MQTT P2P_PORT=$B3_P2P P2P_DISCOVERY_PORT=$B3_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B2_P2P" \
    "$BROKER" >/tmp/tp_b3.log 2>&1 &
B3_PID=$!
sleep "$SETTLE_SEC"

# start subscribers on B2 and B3
for j in $(seq 1 "$SUB_COUNT"); do
    f=$(mktemp /tmp/tp_sub_b2_XXXXXX); SUB_FILES="$SUB_FILES $f"
    "$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/tp/4510/#" \
        -c 99999 > "$f" 2>/dev/null &
    SUB_PIDS="$SUB_PIDS $!"

    f=$(mktemp /tmp/tp_sub_b3_XXXXXX); SUB_FILES="$SUB_FILES $f"
    "$CLI" sub -h 127.0.0.1 -p "$B3_MQTT" -t "site/tp/4510/#" \
        -c 99999 > "$f" 2>/dev/null &
    SUB_PIDS="$SUB_PIDS $!"
done
sleep 0.3

# start publishers on B1
for j in $(seq 1 "$PUB_COUNT"); do
    (end=$(($(date +%s) + DURATION))
     while [ "$(date +%s)" -lt "$end" ]; do
         "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
             -t "site/tp/4510/io" -m "tp" >/dev/null 2>&1 || true
     done) &
    PUB_PIDS="$PUB_PIDS $!"
done

printf '  Publishing for %ds...\n' "$DURATION"
sleep "$DURATION"

# stop publishers
for pid in $PUB_PIDS; do kill "$pid" 2>/dev/null || true; done
sleep 0.5
# stop subscribers
for pid in $SUB_PIDS; do kill "$pid" 2>/dev/null || true; done
wait 2>/dev/null || true

# count total received messages across all sub files
total=0
for f in $SUB_FILES; do
    c=$(wc -l < "$f" 2>/dev/null || echo 0)
    total=$((total + c))
done
per_sec=$((total / DURATION))

printf '  Total received: %d msgs (%d msg/s across all subs)\n' \
    "$total" "$per_sec"

if [ "$total" -ge "$MIN_THROUGHPUT_MSG" ]; then
    ok "throughput: $total msgs >= $MIN_THROUGHPUT_MSG minimum"
else
    fail "throughput: only $total msgs (< $MIN_THROUGHPUT_MSG minimum)"
fi

# Verify all three brokers still alive
for pid in $B1_PID $B2_PID $B3_PID; do
    if kill -0 "$pid" 2>/dev/null; then
        ok "broker PID $pid survived stress"
    else
        fail "broker PID $pid crashed during stress"
    fi
done

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
