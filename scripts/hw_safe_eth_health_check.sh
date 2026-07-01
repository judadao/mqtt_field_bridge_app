#!/usr/bin/env bash
# Low-impact Ethernet health check for ESP32 W5500 field bridge nodes.
#
# This intentionally avoids raw TCP port probes. A bare TCP connect to the
# ESP32 HTTP port can occupy a scarce Zephyr HTTP client slot without sending a
# request, which makes later real HTTP checks look like firmware failures.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BROKER_DIR="$ROOT_DIR/deps/mqtt_min_broker"
CLI="$BROKER_DIR/build_out/mqtt_cli"
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware/safe-eth-health-$(date +%Y%m%d-%H%M%S)"}
HOSTS_CSV=${HOSTS:-192.168.127.10,192.168.127.11,192.168.127.12,192.168.127.13,192.168.127.14}
HTTP_PORT=${HTTP_PORT:-8080}
MQTT_PORT=${MQTT_PORT:-1883}
PING_COUNT=${PING_COUNT:-2}
PING_INTERVAL=${PING_INTERVAL:-0.5}
HTTP_RETRIES=${HTTP_RETRIES:-3}
HTTP_CONNECT_TIMEOUT=${HTTP_CONNECT_TIMEOUT:-2}
HTTP_MAX_TIME=${HTTP_MAX_TIME:-8}
NODE_DELAY=${NODE_DELAY:-10}
RUN_MQTT=${RUN_MQTT:-1}
MQTT_QOS=${MQTT_QOS:-0}
TOPIC_PREFIX=${TOPIC_PREFIX:-site/field-a/safe-health}

mkdir -p "$LOG_DIR"

IFS=',' read -r -a HOSTS <<<"$HOSTS_CSV"

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'error: missing command: %s\n' "$1" >&2
        exit 1
    }
}

json_value() {
    key=$1
    file=$2
    sed -n "s/.*\"$key\":\"\\([^\"]*\\)\".*/\\1/p" "$file" | head -n1
}

need curl
need ping
need sed
need timeout

if [ "$RUN_MQTT" = 1 ] && [ ! -x "$CLI" ]; then
    need rg
    make -C "$BROKER_DIR" -f Makefile.linux P2P=1 all
elif [ "$RUN_MQTT" = 1 ]; then
    need rg
fi

summary="$LOG_DIR/summary.log"
pass=0
fail=0

record_pass() {
    pass=$((pass + 1))
    printf 'PASS %s\n' "$*" | tee -a "$summary"
}

record_fail() {
    fail=$((fail + 1))
    printf 'FAIL %s\n' "$*" | tee -a "$summary"
}

printf 'safe ESP32 Ethernet health check\n' | tee "$summary"
printf 'hosts=%s http=%s mqtt=%s log_dir=%s\n' \
    "$HOSTS_CSV" "$HTTP_PORT" "$MQTT_PORT" "$LOG_DIR" | tee -a "$summary"

for host in "${HOSTS[@]}"; do
    [ -n "$host" ] || continue
    printf '\n== %s ==\n' "$host" | tee -a "$summary"

    ping_log="$LOG_DIR/ping-$host.log"
    if ping -c "$PING_COUNT" -i "$PING_INTERVAL" -W 2 "$host" >"$ping_log" 2>&1; then
        tail -2 "$ping_log" | tee -a "$summary"
        record_pass "$host ping"
    else
        tail -2 "$ping_log" | tee -a "$summary" || true
        record_fail "$host ping"
        sleep "$NODE_DELAY"
        continue
    fi

    status_file="$LOG_DIR/status-$host.json"
    http_ok=0
    for attempt in $(seq 1 "$HTTP_RETRIES"); do
        if curl -fsS --connect-timeout "$HTTP_CONNECT_TIMEOUT" --max-time "$HTTP_MAX_TIME" \
            "http://$host:$HTTP_PORT/status" >"$status_file" 2>"$LOG_DIR/status-$host-attempt$attempt.err"; then
            http_ok=1
            break
        fi
        sleep 2
    done

    if [ "$http_ok" = 1 ]; then
        fw=$(json_value firmware_version "$status_file")
        cfg=$(sed -n 's/.*"config_version":\([0-9][0-9]*\).*/\1/p' "$status_file" | head -n1)
        broker=$(json_value broker_state "$status_file")
        peers=$(sed -n 's/.*"connected_peers":\([0-9][0-9]*\).*/\1/p' "$status_file" | head -n1)
        printf 'status firmware=%s config=%s broker=%s peers=%s\n' \
            "${fw:-?}" "${cfg:-?}" "${broker:-?}" "${peers:-?}" | tee -a "$summary"
        record_pass "$host http-status"
    else
        cat "$LOG_DIR/status-$host-attempt$HTTP_RETRIES.err" >>"$summary" 2>/dev/null || true
        record_fail "$host http-status"
    fi

    if [ "$RUN_MQTT" = 1 ] && [ -x "$CLI" ]; then
        mqtt_log="$LOG_DIR/mqtt-$host.log"
        topic="$TOPIC_PREFIX/$host/$(date +%s)"
        payload="safe-health-$host-$(date +%s)"
        rm -f "$mqtt_log"
        timeout 10 "$CLI" sub -h "$host" -p "$MQTT_PORT" -t "$topic" -q "$MQTT_QOS" \
            >"$mqtt_log" 2>&1 &
        sub_pid=$!
        sleep 1
        "$CLI" pub -h "$host" -p "$MQTT_PORT" -t "$topic" -m "$payload" -q "$MQTT_QOS" \
            >>"$mqtt_log" 2>&1 || true
        deadline=$((SECONDS + 5))
        mqtt_ok=0
        while [ "$SECONDS" -lt "$deadline" ]; do
            if rg -q --fixed-strings "$payload" "$mqtt_log" 2>/dev/null; then
                mqtt_ok=1
                break
            fi
            sleep 1
        done
        kill "$sub_pid" >/dev/null 2>&1 || true
        wait "$sub_pid" >/dev/null 2>&1 || true
        if [ "$mqtt_ok" = 1 ]; then
            record_pass "$host mqtt-pubsub"
        else
            record_fail "$host mqtt-pubsub"
        fi
    fi

    sleep "$NODE_DELAY"
done

printf '\nresult pass=%u fail=%u\n' "$pass" "$fail" | tee -a "$summary"
[ "$fail" -eq 0 ]
