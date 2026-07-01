#!/usr/bin/env bash
# MQTT client wrapper that tries a primary broker port first, then a fallback
# MQTT port on the same host when the primary connect/CONNACK/sub/pub fails.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CLI=${MQTT_CLI:-"$ROOT_DIR/deps/mqtt_min_broker/build_out/mqtt_cli"}

MODE=""
HOST="127.0.0.1"
BROKER_PORT=1883
FALLBACK_PORT=1884
CLIENT_ID="fallback_client_$$"
TOPIC=""
MESSAGE=""
QOS=0
RETAIN=0
CLEAN_SESSION=1

usage() {
    cat >&2 <<EOF
Usage:
  $0 pub --host HOST --broker-port PORT --fallback-port PORT --topic TOPIC --message MSG [--id ID] [--qos 0|1|2] [--retain] [--persistent]
  $0 sub --host HOST --broker-port PORT --fallback-port PORT --topic TOPIC [--id ID] [--qos 0|1|2] [--persistent]
EOF
}

need_cli() {
    if [ ! -x "$CLI" ]; then
        make -C "$ROOT_DIR/deps/mqtt_min_broker" -f Makefile.linux all >/dev/null
    fi
    [ -x "$CLI" ] || {
        printf 'error: mqtt_cli not found: %s\n' "$CLI" >&2
        exit 1
    }
}

run_cli() {
    port=$1
    shift

    args=(-h "$HOST" -p "$port" -i "$CLIENT_ID" -t "$TOPIC" -q "$QOS")
    if [ "$CLEAN_SESSION" -eq 0 ]; then
        args+=(-s)
    fi

    if [ "$MODE" = "pub" ]; then
        args+=(-m "$MESSAGE")
        if [ "$RETAIN" -eq 1 ]; then
            args+=(-r)
        fi
    fi

    "$CLI" "$MODE" "${args[@]}" "$@"
}

try_then_fallback() {
    primary_log=$(mktemp)
    trap 'rm -f "$primary_log"' EXIT

    if run_cli "$BROKER_PORT" 2>"$primary_log"; then
        printf 'selected=primary host=%s port=%u\n' "$HOST" "$BROKER_PORT" >&2
        return 0
    fi

    primary_rc=$?
    printf 'primary_failed host=%s port=%u rc=%d\n' "$HOST" "$BROKER_PORT" "$primary_rc" >&2
    sed 's/^/primary: /' "$primary_log" >&2

    run_cli "$FALLBACK_PORT"
    printf 'selected=fallback host=%s port=%u\n' "$HOST" "$FALLBACK_PORT" >&2
}

if [ $# -lt 1 ]; then
    usage
    exit 1
fi

MODE=$1
shift
case "$MODE" in
    pub|sub) ;;
    *) usage; exit 1 ;;
esac

while [ $# -gt 0 ]; do
    case "$1" in
        --host) HOST=$2; shift 2 ;;
        --broker-port) BROKER_PORT=$2; shift 2 ;;
        --fallback-port) FALLBACK_PORT=$2; shift 2 ;;
        --id) CLIENT_ID=$2; shift 2 ;;
        --topic) TOPIC=$2; shift 2 ;;
        --message) MESSAGE=$2; shift 2 ;;
        --qos) QOS=$2; shift 2 ;;
        --retain) RETAIN=1; shift ;;
        --persistent) CLEAN_SESSION=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'error: unknown option: %s\n' "$1" >&2; usage; exit 1 ;;
    esac
done

if [ -z "$TOPIC" ]; then
    printf 'error: --topic is required\n' >&2
    exit 1
fi
if [ "$MODE" = "pub" ] && [ -z "$MESSAGE" ]; then
    printf 'error: --message is required for pub\n' >&2
    exit 1
fi
if [ "$BROKER_PORT" = "$FALLBACK_PORT" ]; then
    printf 'error: --broker-port and --fallback-port must differ\n' >&2
    exit 1
fi

need_cli
try_then_fallback
