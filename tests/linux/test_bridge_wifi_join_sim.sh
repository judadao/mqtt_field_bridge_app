#!/usr/bin/env bash
# test_bridge_wifi_join_sim.sh — Linux mock for Bridge WiFi selection workflow.
#
# This does not emulate ESP32 SoftAP/STA radio behavior. It simulates the product
# control-plane flow that the future UI will use after scanning bridge SSIDs:
# choose a discovered bridge node, then write the matching peer index.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TEST_DIR="$ROOT_DIR/tests/linux"
APP_SRC="$ROOT_DIR/app/src"
BROKER="$ROOT_DIR/deps/mqtt_min_broker"
OUT="$TEST_DIR/out/bridge_wifi_sim"

PASS=0
FAIL=0
PIDS=""

ok() { PASS=$((PASS + 1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1"; }

cleanup() {
    for pid in $PIDS; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT"
rm -f "$OUT"/*.bin "$OUT"/*.log

build_server() {
    name=$1
    http_port=$2
    target="$OUT/web_${name}"

    gcc -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
        -DPROVISIONING_HTTP_PORT="$http_port" \
        -I"$APP_SRC" -I"$TEST_DIR/include" -I"$BROKER/include" \
        "$TEST_DIR/run_web_config_server.c" \
        "$APP_SRC/provisioning_http.c" \
        "$APP_SRC/product_config.c" \
        "$APP_SRC/product_runtime.c" \
        "$APP_SRC/product_topics.c" \
        "$APP_SRC/bridge_control.c" \
        -o "$target" -lpthread
}

start_node() {
    name=$1
    http_port=$2
    peers_file="$OUT/${name}_peers.bin"
    settings_file="$OUT/${name}_settings.bin"

    BRIDGE_PEERS_FILE="$peers_file" \
    BRIDGE_SETTINGS_FILE="$settings_file" \
    BRIDGE_WIFI_FILE="$OUT/${name}_bridge_wifi.bin" \
        "$OUT/web_${name}" >"$OUT/${name}.log" 2>&1 &
    PIDS="$PIDS $!"
}

wait_http() {
    port=$1
    deadline=$((SECONDS + 5))
    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS "http://127.0.0.1:${port}/status" >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    return 1
}

login() {
    port=$1
    curl -fsS -X POST "http://127.0.0.1:${port}/login" \
        -H 'Content-Type: application/json' \
        -d '{"password":"admin"}' \
        | sed -n 's/.*"token":"\([^"]*\)".*/\1/p'
}

post_config() {
    port=$1
    token=$2
    name=$3
    mqtt_port=$4
    p2p_port=$5

    curl -fsS -X POST "http://127.0.0.1:${port}/config" \
        -H "X-Auth-Token: $token" \
        -H 'Content-Type: application/json' \
        -d "{\"device_name\":\"${name}\",\"admin_password\":\"admin\",\
\"wifi_ssid\":\"\",\"wifi_password\":\"\",\
\"ap_ssid\":\"MQTT-BRIDGE-${name}\",\"ap_password\":\"12345678\",\
\"device_ip\":\"192.168.4.1\",\"gateway\":\"192.168.4.1\",\
\"netmask\":\"255.255.255.0\",\"dns\":\"192.168.4.1\",\
\"dhcp_enabled\":0,\"site_id\":\"field-a\",\
\"topic_prefix\":\"site/field-a\",\"mqtt_port\":${mqtt_port},\
\"p2p_port\":${p2p_port},\"broker_enabled\":1,\
\"bridge_enabled\":1,\"mesh_enabled\":1}" >/dev/null
}

peer_json_for_scan_name() {
    ssid=$1
    case "$ssid" in
        MQTT-BRIDGE-node1)
            printf '{"name":"node1","host":"127.0.0.2","mqtt_port":11883,"p2p_port":14884,"enabled":1}'
            ;;
        MQTT-BRIDGE-node2)
            printf '{"name":"node2","host":"127.0.0.3","mqtt_port":11884,"p2p_port":14886,"enabled":1}'
            ;;
        MQTT-BRIDGE-node3)
            printf '{"name":"node3","host":"127.0.0.4","mqtt_port":11885,"p2p_port":14888,"enabled":1}'
            ;;
        *)
            return 1
            ;;
    esac
}

join_json_for_scan_name() {
    peer_index=$1
    ssid=$2
    case "$ssid" in
        MQTT-BRIDGE-node1)
            printf '{"ssid":"MQTT-BRIDGE-node1","password":"12345678","peer_name":"node1","host":"127.0.0.2","mqtt_port":11883,"p2p_port":14884,"peer_index":%s}' "$peer_index"
            ;;
        MQTT-BRIDGE-node2)
            printf '{"ssid":"MQTT-BRIDGE-node2","password":"12345678","peer_name":"node2","host":"127.0.0.3","mqtt_port":11884,"p2p_port":14886,"peer_index":%s}' "$peer_index"
            ;;
        MQTT-BRIDGE-node3)
            printf '{"ssid":"MQTT-BRIDGE-node3","password":"12345678","peer_name":"node3","host":"127.0.0.4","mqtt_port":11885,"p2p_port":14888,"peer_index":%s}' "$peer_index"
            ;;
        *)
            return 1
            ;;
    esac
}

join_bridge_wifi_mock() {
    local_port=$1
    token=$2
    peer_index=$3
    ssid=$4
    peer_body=$(join_json_for_scan_name "$peer_index" "$ssid")

    curl -fsS -X POST "http://127.0.0.1:${local_port}/bridge-wifi/join" \
        -H "X-Auth-Token: $token" \
        -H 'Content-Type: application/json' \
        -d "$peer_body" >/dev/null
}

bridge_wifi_current() {
    port=$1
    token=$2
    curl -fsS "http://127.0.0.1:${port}/bridge-wifi/current" \
        -H "X-Auth-Token: $token"
}

bridge_wifi_recent() {
    port=$1
    token=$2
    curl -fsS "http://127.0.0.1:${port}/bridge-wifi/recent" \
        -H "X-Auth-Token: $token"
}

delete_peer_index() {
    local_port=$1
    token=$2
    peer_index=$3

    curl -fsS -X POST "http://127.0.0.1:${local_port}/peers/${peer_index}" \
        -H "X-Auth-Token: $token" \
        -H 'Content-Type: application/json' \
        -d '{"name":"","host":"","mqtt_port":1883,"p2p_port":4884,"enabled":0}' >/dev/null
}

peers() {
    port=$1
    token=$2
    curl -fsS "http://127.0.0.1:${port}/peers" -H "X-Auth-Token: $token"
}

contains() {
    haystack=$1
    needle=$2
    printf '%s' "$haystack" | grep -q "$needle"
}

echo "=== test_bridge_wifi_join_sim.sh ==="

build_server node1 18081
build_server node2 18082
build_server node3 18083

fuser -k 18081/tcp 18082/tcp 18083/tcp >/dev/null 2>&1 || true
sleep 0.2

start_node node1 18081
start_node node2 18082
start_node node3 18083

wait_http 18081 && wait_http 18082 && wait_http 18083 \
    && ok "three provisioning nodes started" \
    || { fail "three provisioning nodes did not start"; exit 1; }

T1=$(login 18081)
T2=$(login 18082)
T3=$(login 18083)
[ -n "$T1" ] && [ -n "$T2" ] && [ -n "$T3" ] \
    && ok "all nodes returned auth tokens" \
    || { fail "login tokens missing"; exit 1; }

post_config 18081 "$T1" node1 11883 14884
post_config 18082 "$T2" node2 11884 14886
post_config 18083 "$T3" node3 11885 14888
ok "node identities and broker ports configured"

# Simulate node2 choosing node1 from Bridge WiFi scan results.
join_bridge_wifi_mock 18082 "$T2" 0 MQTT-BRIDGE-node1
P2=$(peers 18082 "$T2")
P2_CUR=$(bridge_wifi_current 18082 "$T2")
contains "$P2" '"name":"node1"' && contains "$P2" '"host":"127.0.0.2"' \
    && contains "$P2" '"p2p_port":14884' \
    && contains "$P2_CUR" '"connected":1' \
    && contains "$P2_CUR" '"local_sta_ip":"127.0.0.10"' \
    && contains "$P2_CUR" '"gateway_ip":"127.0.0.2"' \
    && contains "$P2_CUR" '"peer_broker_ip":"127.0.0.2"' \
    && contains "$P2_CUR" 'MQTT-BRIDGE-node1' \
    && ok "node2 peer index 0 joins node1" \
    || fail "node2 peer index 0 did not join node1"

# Simulate node3 being able to choose either node1 or node2.
join_bridge_wifi_mock 18083 "$T3" 0 MQTT-BRIDGE-node1
join_bridge_wifi_mock 18083 "$T3" 1 MQTT-BRIDGE-node2
P3=$(peers 18083 "$T3")
P3_RECENT=$(bridge_wifi_recent 18083 "$T3")
contains "$P3" '"name":"node1"' && contains "$P3" '"name":"node2"' \
    && contains "$P3_RECENT" 'MQTT-BRIDGE-node1' \
    && contains "$P3_RECENT" 'MQTT-BRIDGE-node2' \
    && ok "node3 can add both node1 and node2 as selectable bridge peers" \
    || fail "node3 did not retain both selected bridge peers"

# Deleting peer index 1 must not clear peer index 0.
delete_peer_index 18083 "$T3" 1
P3_AFTER_DELETE=$(peers 18083 "$T3")
contains "$P3_AFTER_DELETE" '"name":"node1"' \
    && ! contains "$P3_AFTER_DELETE" '"name":"node2"' \
    && ok "delete peer index 1 leaves peer index 0 intact" \
    || fail "delete peer index 1 affected the wrong peer"

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
