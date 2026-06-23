#!/usr/bin/env bash
# Temporarily leave the Linux AP, connect to the ESP32 SoftAP, and verify the
# provisioning homepage. The previous Wi-Fi connection is restored by default.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
WIFI_IFACE=${WIFI_IFACE:-}
ESP32_AP_SSID=${ESP32_AP_SSID:-ESP32-Min-Broker}
ESP32_AP_PASS=${ESP32_AP_PASS:-12345678}
ESP32_HTTP=${ESP32_HTTP:-http://192.168.4.1:8080}
ADMIN_PASSWORD=${ADMIN_PASSWORD:-admin}
RESTORE_WIFI=${RESTORE_WIFI:-1}
SCAN_RETRIES=${SCAN_RETRIES:-8}
WAIT_SECONDS=${WAIT_SECONDS:-45}
ITERATIONS=${ITERATIONS:-1}
CONNECT_RETRIES=${CONNECT_RETRIES:-5}

mkdir -p "$LOG_DIR"
RUN_LOG="$LOG_DIR/esp32-homepage-$(date +%Y%m%d-%H%M%S).log"
INDEX_HTML="$LOG_DIR/esp32-homepage-index.html"
STATUS_JSON="$LOG_DIR/esp32-homepage-status.json"
CONFIG_JSON="$LOG_DIR/esp32-homepage-config.json"
SCAN_JSON="$LOG_DIR/esp32-homepage-scan.json"
DEVICE_IP="$LOG_DIR/esp32-homepage-device.txt"
exec > >(tee "$RUN_LOG") 2>&1

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'error: missing command: %s\n' "$1" >&2
        exit 1
    }
}

need curl
need jq
need nmcli
need rg

if [ -z "$WIFI_IFACE" ]; then
    WIFI_IFACE=$(nmcli -t -f DEVICE,TYPE device status |
        awk -F: '$2 == "wifi" { print $1; exit }')
fi
[ -n "$WIFI_IFACE" ] || {
    printf 'error: no Wi-Fi interface found by NetworkManager\n' >&2
    exit 1
}

PREV_CON=$(nmcli -t -f NAME,DEVICE con show --active |
    awk -F: -v dev="$WIFI_IFACE" '$2 == dev { print $1; exit }')

restore_wifi() {
    if [ "$RESTORE_WIFI" = "1" ] && [ -n "${PREV_CON:-}" ]; then
        printf '\nRestoring Wi-Fi connection: %s\n' "$PREV_CON"
        nmcli con up "$PREV_CON" ifname "$WIFI_IFACE" >/dev/null 2>&1 || {
            printf 'warning: failed to restore Wi-Fi connection %s\n' "$PREV_CON" >&2
        }
    fi
}
trap restore_wifi EXIT

wait_http() {
    local deadline=$((SECONDS + WAIT_SECONDS))

    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS --max-time 4 "$ESP32_HTTP/status" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

wait_for_ssid() {
    local i

    for i in $(seq 1 "$SCAN_RETRIES"); do
        printf '\nScan %s/%s for %s\n' "$i" "$SCAN_RETRIES" "$ESP32_AP_SSID"
        nmcli device wifi rescan ifname "$WIFI_IFACE" ssid "$ESP32_AP_SSID" >/dev/null 2>&1 || true
        sleep 3
        nmcli -f SSID,CHAN,SIGNAL,SECURITY device wifi list ifname "$WIFI_IFACE" |
            sed -n '1,20p'
        if nmcli -t -f SSID device wifi list ifname "$WIFI_IFACE" |
            rg -x --fixed-strings "$ESP32_AP_SSID" >/dev/null; then
            return 0
        fi
    done
    return 1
}

connect_esp32_ap() {
    local i

    for i in $(seq 1 "$CONNECT_RETRIES"); do
        printf '\nConnect attempt %s/%s to %s\n' "$i" "$CONNECT_RETRIES" "$ESP32_AP_SSID"
        nmcli device wifi rescan ifname "$WIFI_IFACE" ssid "$ESP32_AP_SSID" >/dev/null 2>&1 || true
        sleep 2
        if nmcli device wifi connect "$ESP32_AP_SSID" password "$ESP32_AP_PASS" ifname "$WIFI_IFACE"; then
            return 0
        fi
        sleep 2
    done
    return 1
}

printf 'Wi-Fi interface: %s\n' "$WIFI_IFACE"
printf 'Previous Wi-Fi:  %s\n' "${PREV_CON:-none}"
printf 'ESP32 SSID:      %s\n' "$ESP32_AP_SSID"
printf 'ESP32 HTTP:      %s\n' "$ESP32_HTTP"
printf 'Run log:         %s\n' "$RUN_LOG"

nmcli radio wifi on
nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
sleep 5

wait_for_ssid || {
    printf 'error: SSID not found: %s\n' "$ESP32_AP_SSID" >&2
    exit 1
}

connect_esp32_ap || {
    printf 'error: failed to connect to SSID: %s\n' "$ESP32_AP_SSID" >&2
    exit 1
}
nmcli -f IP4 device show "$WIFI_IFACE" | tee "$DEVICE_IP"

wait_http || {
    printf 'error: ESP32 HTTP did not answer at %s\n' "$ESP32_HTTP" >&2
    exit 1
}

for i in $(seq 1 "$ITERATIONS"); do
    printf '\nHTTP iteration %s/%s\n' "$i" "$ITERATIONS"
    curl -fsS --max-time 10 \
        -w "\nstatus_time=%{time_total}s status_size=%{size_download}B\n" \
        "$ESP32_HTTP/status" |
        tee "$STATUS_JSON"
    printf '\n'
    curl -fsS --max-time 10 \
        -w "homepage_time=%{time_total}s homepage_size=%{size_download}B\n" \
        "$ESP32_HTTP/" -o "$INDEX_HTML"
done

rg --fixed-strings 'Field Bridge Settings' "$INDEX_HTML" >/dev/null || {
    printf 'error: homepage did not contain expected title\n' >&2
    exit 1
}
rg --fixed-strings '/status' "$INDEX_HTML" >/dev/null || {
    printf 'error: homepage did not contain expected status client code\n' >&2
    exit 1
}
rg --fixed-strings "function W()" "$INDEX_HTML" >/dev/null || {
    printf 'error: script did not contain expected scan function\n' >&2
    exit 1
}

printf '\nAuthenticated API checks\n'
TOKEN=$(curl -fsS --max-time 10 -X POST "$ESP32_HTTP/login" \
    -H 'Content-Type: application/json' \
    -d "{\"password\":\"$ADMIN_PASSWORD\"}" |
    jq -r '.token // empty')
[ -n "$TOKEN" ] || {
    printf 'error: login failed; no auth token returned\n' >&2
    exit 1
}
curl -fsS --max-time 10 -H "X-Auth-Token: $TOKEN" "$ESP32_HTTP/config" |
    tee "$CONFIG_JSON" >/dev/null
jq -e '.device_name and .ap_ssid and .mqtt_port' "$CONFIG_JSON" >/dev/null
curl -fsS --max-time 25 -H "X-Auth-Token: $TOKEN" "$ESP32_HTTP/wifi/scan" |
    tee "$SCAN_JSON" >/dev/null
SCAN_COUNT=$(jq 'length' "$SCAN_JSON")
if [ "$SCAN_COUNT" -le 0 ]; then
    printf 'error: ESP32 Wi-Fi scan returned no AP entries\n' >&2
    exit 1
fi
printf 'Authenticated API checks passed; scan_count=%s\n' "$SCAN_COUNT"

printf '\nESP32 homepage test passed.\n'
printf 'Saved status: %s\n' "$STATUS_JSON"
printf 'Saved index:  %s\n' "$INDEX_HTML"
