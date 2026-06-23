#!/usr/bin/env bash
# Validate the ESP32 SoftAP web status and real Wi-Fi scan endpoint.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
WIFI_IFACE=${WIFI_IFACE:-}
ESP32_AP_SSID=${ESP32_AP_SSID:-ESP32-Min-Broker}
ESP32_AP_PASS=${ESP32_AP_PASS:-12345678}
ESP32_HTTP=${ESP32_HTTP:-http://192.168.4.1:8080}
ADMIN_PASSWORD=${ADMIN_PASSWORD:-admin}
WAIT_SECONDS=${WAIT_SECONDS:-45}

mkdir -p "$LOG_DIR"
RUN_LOG="$LOG_DIR/esp32-scan-$(date +%Y%m%d-%H%M%S).log"
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

if [ -z "$WIFI_IFACE" ]; then
    WIFI_IFACE=$(nmcli -t -f DEVICE,TYPE device status |
        awk -F: '$2 == "wifi" { print $1; exit }')
fi
[ -n "$WIFI_IFACE" ] || {
    printf 'error: no Wi-Fi interface found by NetworkManager\n' >&2
    exit 1
}

wait_http() {
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS --max-time 4 "$ESP32_HTTP/status" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

printf 'Wi-Fi interface: %s\n' "$WIFI_IFACE"
printf 'Run log:         %s\n' "$RUN_LOG"

nmcli radio wifi on
nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
sleep 2
nmcli device wifi rescan ifname "$WIFI_IFACE" ssid "$ESP32_AP_SSID" >/dev/null 2>&1 || true
sleep 2
nmcli device wifi connect "$ESP32_AP_SSID" password "$ESP32_AP_PASS" ifname "$WIFI_IFACE"
nmcli -f IP4 device show "$WIFI_IFACE" | tee "$LOG_DIR/esp32-scan-device.txt"

wait_http || {
    printf 'error: ESP32 HTTP did not answer at %s\n' "$ESP32_HTTP" >&2
    exit 1
}

curl -fsS --max-time 8 "$ESP32_HTTP/status" |
    tee "$LOG_DIR/esp32-scan-status.json"

TOKEN=$(curl -fsS --max-time 8 -X POST "$ESP32_HTTP/login" \
    -H 'Content-Type: application/json' \
    -d "{\"password\":\"$ADMIN_PASSWORD\"}" |
    jq -r '.token // empty')
[ -n "$TOKEN" ] || {
    printf 'error: login failed; no auth token returned\n' >&2
    exit 1
}

curl -fsS --max-time 20 -H "X-Auth-Token: $TOKEN" "$ESP32_HTTP/wifi/scan" |
    tee "$LOG_DIR/esp32-scan-results.json"

SCAN_COUNT=$(jq 'length' "$LOG_DIR/esp32-scan-results.json")
if [ "$SCAN_COUNT" -le 0 ]; then
    printf 'error: ESP32 Wi-Fi scan returned no AP entries\n' >&2
    exit 1
fi

printf '\nESP32 Wi-Fi scan test passed with %s result(s).\n' "$SCAN_COUNT"
