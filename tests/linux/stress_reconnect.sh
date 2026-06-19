#!/usr/bin/env sh
# stress_reconnect.sh — reconnect stress test.
#
# Publisher on B1 publishes at ~200 msg/s.
# B2 is killed and restarted RESTART_COUNT times.
# After each restart, B2 must re-subscribe and receive messages.
# B1 must never hang or crash.
#
# Knobs:
#   RESTART_COUNT  number of kill+restart cycles (default: 10)
#   PUB_RATE_MS    sleep between publishes in ms (default: 5)
#   SETTLE_SEC     P2P re-mesh wait after restart (default: 3)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
BROKER="$BROKER_DIR/build_out/mqtt_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"

RESTART_COUNT=${RESTART_COUNT:-10}
SETTLE_SEC=${SETTLE_SEC:-3}
VERIFY_MSGS=3

PASS=0; FAIL=0
B1_PID="" B2_PID=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID ${PUB_PID:-}; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ ! -x "$BROKER" ]; then
    printf 'Building broker with P2P...\n'
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all >/dev/null 2>&1
fi

B1_MQTT=12883; B1_P2P=15884; B1_DISC=15885
B2_MQTT=12884; B2_P2P=15886; B2_DISC=15887

echo "=== stress_reconnect.sh (${RESTART_COUNT} cycles) ==="

MQTT_BROKER_PORT=$B1_MQTT P2P_PORT=$B1_P2P P2P_DISCOVERY_PORT=$B1_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B2_P2P" \
    "$BROKER" >/tmp/stress_b1.log 2>&1 &
B1_PID=$!

MQTT_BROKER_PORT=$B2_MQTT P2P_PORT=$B2_P2P P2P_DISCOVERY_PORT=$B2_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P" \
    "$BROKER" >/tmp/stress_b2.log 2>&1 &
B2_PID=$!
sleep "$SETTLE_SEC"

# background publisher on B1 — publishes continuously
(while true; do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
        -t "site/stress/4510/io" -m "stress" >/dev/null 2>&1 || true
    sleep 0.005
done) &
PUB_PID=$!

# B1 should stay alive throughout
b1_alive_before=$RESTART_COUNT

for i in $(seq 1 "$RESTART_COUNT"); do
    printf '  cycle %d/%d: kill B2...\n' "$i" "$RESTART_COUNT"
    kill "$B2_PID" 2>/dev/null || true
    wait "$B2_PID" 2>/dev/null || true

    # B1 must still be running
    if ! kill -0 "$B1_PID" 2>/dev/null; then
        fail "cycle $i: B1 crashed when B2 was killed"
        b1_alive_before=$((b1_alive_before - 1))
    fi

    sleep 0.2
    # restart B2
    MQTT_BROKER_PORT=$B2_MQTT P2P_PORT=$B2_P2P P2P_DISCOVERY_PORT=$B2_DISC \
        MQTT_P2P_PEERS="127.0.0.1:$B1_P2P" \
        "$BROKER" >/tmp/stress_b2_$i.log 2>&1 &
    B2_PID=$!
    sleep "$SETTLE_SEC"

    # verify B2 can receive messages after recovery
    RECV_FILE=$(mktemp /tmp/recv_stress_XXXXXX)
    "$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/stress/4510/#" \
        -c "$VERIFY_MSGS" > "$RECV_FILE" 2>/dev/null &
    SUB_PID=$!
    sleep 1
    kill "$SUB_PID" 2>/dev/null || true
    wait "$SUB_PID" 2>/dev/null || true
    recv_count=$(wc -l < "$RECV_FILE" || echo 0)
    rm -f "$RECV_FILE"

    if [ "$recv_count" -ge "$VERIFY_MSGS" ]; then
        ok "cycle $i: B2 recovered and received $recv_count msgs"
    else
        fail "cycle $i: B2 only received $recv_count/$VERIFY_MSGS msgs after restart"
    fi
done

# Final check: B1 still alive
if kill -0 "$B1_PID" 2>/dev/null; then
    ok "B1 survived all $RESTART_COUNT restart cycles"
else
    fail "B1 crashed during stress test"
fi

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
