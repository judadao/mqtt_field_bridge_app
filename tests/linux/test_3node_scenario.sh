#!/usr/bin/env sh
# test_3node_scenario.sh — integration test for the Note1/Note2/Note3 field scenario.
#
# Simulates the three-broker P2P mesh on localhost using port offsets.
# Requires: deps/mqtt_min_broker built with P2P support (make -f Makefile.linux P2P=1)
#
# Run:
#   ./tests/linux/test_3node_scenario.sh
#
# Knobs:
#   SETTLE_SEC   how long to wait for P2P mesh formation (default: 3)
#   MSG_COUNT    messages to publish per topic (default: 5)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
BROKER="$BROKER_DIR/build_out/mqtt_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"

SETTLE_SEC=${SETTLE_SEC:-3}
MSG_COUNT=${MSG_COUNT:-5}

PASS=0; FAIL=0
B1_PID="" B2_PID="" B3_PID=""

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $B1_PID $B2_PID $B3_PID; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# ── build broker with P2P if not already built ───────────────────────────
if [ ! -x "$BROKER" ]; then
    printf 'Building broker with P2P...\n'
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all >/dev/null 2>&1
fi

# ── port layout ───────────────────────────────────────────────────────────
# Note 1 (B1): MQTT=11883, P2P=14884, discovery=14885
# Note 2 (B2): MQTT=11884, P2P=14886, discovery=14887
# Note 3 (B3): MQTT=11885, P2P=14888, discovery=14889

B1_MQTT=11883; B1_P2P=14884; B1_DISC=14885
B2_MQTT=11884; B2_P2P=14886; B2_DISC=14887
B3_MQTT=11885; B3_P2P=14888; B3_DISC=14889

echo "=== test_3node_scenario.sh ==="

# ── start three brokers ───────────────────────────────────────────────────
MQTT_BROKER_PORT=$B1_MQTT P2P_PORT=$B1_P2P P2P_DISCOVERY_PORT=$B1_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B2_P2P,127.0.0.1:$B3_P2P" \
    "$BROKER" >/tmp/b1.log 2>&1 &
B1_PID=$!

MQTT_BROKER_PORT=$B2_MQTT P2P_PORT=$B2_P2P P2P_DISCOVERY_PORT=$B2_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$BROKER" >/tmp/b2.log 2>&1 &
B2_PID=$!

MQTT_BROKER_PORT=$B3_MQTT P2P_PORT=$B3_P2P P2P_DISCOVERY_PORT=$B3_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B2_P2P" \
    "$BROKER" >/tmp/b3.log 2>&1 &
B3_PID=$!

printf 'Waiting %ds for P2P mesh formation...\n' "$SETTLE_SEC"
sleep "$SETTLE_SEC"

# verify all three are still running
for pid in $B1_PID $B2_PID $B3_PID; do
    if ! kill -0 "$pid" 2>/dev/null; then
        fail "broker PID $pid exited early"
    fi
done

# ── T1: Note2 subscriber receives Note1 publish ──────────────────────────
# Subscribe on B2 first (background), then publish on B1.
RECV_FILE=$(mktemp /tmp/recv_XXXXXX)
"$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/field-a/4510/#" \
    -c "$MSG_COUNT" > "$RECV_FILE" 2>/dev/null &
SUB_PID=$!
sleep 0.3
for i in $(seq 1 "$MSG_COUNT"); do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
        -t "site/field-a/4510/io" -m "payload$i" >/dev/null 2>&1
done
sleep 1
kill "$SUB_PID" 2>/dev/null || true
wait "$SUB_PID" 2>/dev/null || true
recv_count=$(wc -l < "$RECV_FILE" || echo 0)
if [ "$recv_count" -ge "$MSG_COUNT" ]; then
    ok "T1: Note2 receives Note1 4510 publish ($recv_count/$MSG_COUNT)"
else
    fail "T1: Note2 only received $recv_count/$MSG_COUNT messages"
fi
rm -f "$RECV_FILE"

# ── T2: Note3 subscriber receives Note1 publish ──────────────────────────
RECV_FILE=$(mktemp /tmp/recv_XXXXXX)
"$CLI" sub -h 127.0.0.1 -p "$B3_MQTT" -t "site/field-a/4510/#" \
    -c "$MSG_COUNT" > "$RECV_FILE" 2>/dev/null &
SUB_PID=$!
sleep 0.3
for i in $(seq 1 "$MSG_COUNT"); do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
        -t "site/field-a/4510/io" -m "payload$i" >/dev/null 2>&1
done
sleep 1
kill "$SUB_PID" 2>/dev/null || true
wait "$SUB_PID" 2>/dev/null || true
recv_count=$(wc -l < "$RECV_FILE" || echo 0)
if [ "$recv_count" -ge "$MSG_COUNT" ]; then
    ok "T2: Note3 receives Note1 4510 publish ($recv_count/$MSG_COUNT)"
else
    fail "T2: Note3 only received $recv_count/$MSG_COUNT messages"
fi
rm -f "$RECV_FILE"

# ── T3: Note1 local subscriber works when Note2 is offline ───────────────
kill "$B2_PID" 2>/dev/null || true; wait "$B2_PID" 2>/dev/null || true; B2_PID=""
sleep 0.5

RECV_FILE=$(mktemp /tmp/recv_XXXXXX)
"$CLI" sub -h 127.0.0.1 -p "$B1_MQTT" -t "site/field-a/4510/#" \
    -c "$MSG_COUNT" > "$RECV_FILE" 2>/dev/null &
SUB_PID=$!
sleep 0.2
for i in $(seq 1 "$MSG_COUNT"); do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
        -t "site/field-a/4510/io" -m "local$i" >/dev/null 2>&1
done
sleep 0.5
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true
recv_count=$(wc -l < "$RECV_FILE" || echo 0)
if [ "$recv_count" -ge "$MSG_COUNT" ]; then
    ok "T3: Note1 local delivery works when Note2 offline ($recv_count/$MSG_COUNT)"
else
    fail "T3: Note1 local delivery degraded ($recv_count/$MSG_COUNT)"
fi
rm -f "$RECV_FILE"

# ── T4: Note2 recovers and resumes receiving after restart ─────────────
MQTT_BROKER_PORT=$B2_MQTT P2P_PORT=$B2_P2P P2P_DISCOVERY_PORT=$B2_DISC \
    MQTT_P2P_PEERS="127.0.0.1:$B1_P2P,127.0.0.1:$B3_P2P" \
    "$BROKER" >/tmp/b2_restart.log 2>&1 &
B2_PID=$!
sleep "$SETTLE_SEC"

RECV_FILE=$(mktemp /tmp/recv_XXXXXX)
"$CLI" sub -h 127.0.0.1 -p "$B2_MQTT" -t "site/field-a/4510/#" \
    -c "$MSG_COUNT" > "$RECV_FILE" 2>/dev/null &
SUB_PID=$!
sleep 0.3
for i in $(seq 1 "$MSG_COUNT"); do
    "$CLI" pub -h 127.0.0.1 -p "$B1_MQTT" \
        -t "site/field-a/4510/io" -m "after_restart$i" >/dev/null 2>&1
done
sleep 1
kill "$SUB_PID" 2>/dev/null || true; wait "$SUB_PID" 2>/dev/null || true
recv_count=$(wc -l < "$RECV_FILE" || echo 0)
if [ "$recv_count" -ge "$MSG_COUNT" ]; then
    ok "T4: Note2 receives after restart ($recv_count/$MSG_COUNT)"
else
    fail "T4: Note2 failed to recover ($recv_count/$MSG_COUNT)"
fi
rm -f "$RECV_FILE"

# ── summary ──────────────────────────────────────────────────────────────
echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
