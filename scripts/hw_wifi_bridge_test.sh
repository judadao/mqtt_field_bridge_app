#!/usr/bin/env bash
# Hardware Wi-Fi validation for one ESP32 bridge node and one Linux Wi-Fi NIC.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
ESP32_AP_SSID=${ESP32_AP_SSID:-ESP32-Min-Broker}
ESP32_AP_PASS=${ESP32_AP_PASS:-12345678}
ESP32_HTTP=${ESP32_HTTP:-http://192.168.4.1:8080}
ADMIN_PASSWORD=${ADMIN_PASSWORD:-admin}
LINUX_AP_SSID=${LINUX_AP_SSID:-Linux-Bridge-Test}
LINUX_AP_PASS=${LINUX_AP_PASS:-bridge1234}
LINUX_AP_CHANNEL=${LINUX_AP_CHANNEL:-}
RESET_ESP32_AFTER_HOTSPOT=${RESET_ESP32_AFTER_HOTSPOT:-1}
WAIT_SECONDS=${WAIT_SECONDS:-90}

mkdir -p "$LOG_DIR"
RUN_LOG="$LOG_DIR/wifi-bridge-$(date +%Y%m%d-%H%M%S).log"
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
need ip

detect_wifi_iface() {
    if [ -n "${WIFI_IFACE:-}" ]; then
        printf '%s\n' "$WIFI_IFACE"
        return 0
    fi
    nmcli -t -f DEVICE,TYPE device status |
        awk -F: '$2 == "wifi" { print $1; exit }'
}

http_json() {
    method=$1
    url=$2
    token=${3:-}
    body=${4:-}
    if [ -n "$body" ]; then
        curl -fsS -X "$method" "$url" \
            --max-time 8 \
            ${token:+-H "X-Auth-Token: $token"} \
            -H 'Content-Type: application/json' \
            -d "$body"
    else
        curl -fsS -X "$method" "$url" \
            --max-time 8 \
            ${token:+-H "X-Auth-Token: $token"}
    fi
}

wait_http() {
    url=$1
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        curl -fsS "$url/status" >/dev/null 2>&1 && return 0
        sleep 1
    done
    return 1
}

find_esp_on_linux_ap() {
    iface=$1
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        while read -r ipaddr _; do
            [ -n "$ipaddr" ] || continue
            case "$ipaddr" in
                10.42.0.1|169.254.*) continue ;;
            esac
            if curl -fsS "http://$ipaddr:8080/status" >/dev/null 2>&1; then
                printf '%s\n' "$ipaddr"
                return 0
            fi
        done < <(ip -4 neigh show dev "$iface" | awk '{print $1, $0}')
        sleep 1
    done
    return 1
}

WIFI_IFACE=$(detect_wifi_iface)
[ -n "$WIFI_IFACE" ] || {
    printf 'error: no Wi-Fi interface found by NetworkManager\n' >&2
    exit 1
}

printf 'Wi-Fi interface: %s\n' "$WIFI_IFACE"
printf 'Run log:         %s\n' "$RUN_LOG"

printf '\n[1/6] Enable Wi-Fi and scan for ESP32 AP\n'
nmcli radio wifi on
SCAN_OUT="$LOG_DIR/scan-esp32-ap.txt"
CURRENT_CONN=$(nmcli -g GENERAL.CONNECTION device show "$WIFI_IFACE" || true)
nmcli device wifi list --rescan yes ifname "$WIFI_IFACE" | tee "$SCAN_OUT"
ESP32_AP_CHANNEL_DETECTED=$(nmcli -t -f SSID,CHAN device wifi list --rescan no ifname "$WIFI_IFACE" |
    awk -F: -v ssid="$ESP32_AP_SSID" '$1 == ssid { print $2; exit }')
if [ -z "$LINUX_AP_CHANNEL" ]; then
    LINUX_AP_CHANNEL=${ESP32_AP_CHANNEL_DETECTED:-1}
fi
if [ "$CURRENT_CONN" = "$ESP32_AP_SSID" ]; then
    printf 'Already connected to ESP32 AP: %s\n' "$ESP32_AP_SSID"
elif [ -z "$ESP32_AP_CHANNEL_DETECTED" ]; then
    printf 'error: ESP32 AP SSID not found: %s\n' "$ESP32_AP_SSID" >&2
    exit 1
fi
printf 'Using Linux hotspot channel: %s\n' "$LINUX_AP_CHANNEL"

printf '\n[2/6] Connect Linux host to ESP32 AP: %s\n' "$ESP32_AP_SSID"
nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
sleep 2
nmcli device wifi rescan ifname "$WIFI_IFACE" ssid "$ESP32_AP_SSID" >/dev/null 2>&1 || true
sleep 2
nmcli device wifi connect "$ESP32_AP_SSID" password "$ESP32_AP_PASS" ifname "$WIFI_IFACE"
wait_http "$ESP32_HTTP" || {
    printf 'error: ESP32 HTTP did not answer at %s\n' "$ESP32_HTTP" >&2
    exit 1
}

printf '\n[3/6] Read ESP32 status and Wi-Fi scan API\n'
TOKEN=$(http_json POST "$ESP32_HTTP/login" "" "{\"password\":\"$ADMIN_PASSWORD\"}" |
    jq -r '.token // empty')
[ -n "$TOKEN" ] || {
    printf 'error: login failed; no auth token returned\n' >&2
    exit 1
}
http_json GET "$ESP32_HTTP/status" "$TOKEN" | tee "$LOG_DIR/esp32-status-on-own-ap.json"
http_json GET "$ESP32_HTTP/wifi/scan" "$TOKEN" | tee "$LOG_DIR/esp32-wifi-scan.json"
SCAN_COUNT=$(jq 'length' "$LOG_DIR/esp32-wifi-scan.json")
if [ "$SCAN_COUNT" -le 0 ]; then
    printf 'error: ESP32 Wi-Fi scan returned no AP entries\n' >&2
    exit 1
fi
printf 'ESP32 Wi-Fi scan entries: %s\n' "$SCAN_COUNT"

printf '\n[4/6] Configure ESP32 station target to Linux hotspot\n'
CONFIG=$(jq -nc \
    --arg ssid "$LINUX_AP_SSID" \
    --arg pass "$LINUX_AP_PASS" \
    '{
      device_name:"esp32-linux-bridge-test",
      admin_password:"admin",
      wifi_ssid:$ssid,
      wifi_password:$pass,
      ap_ssid:"ESP32-Min-Broker",
      ap_password:"12345678",
      device_ip:"192.168.4.1",
      gateway:"192.168.4.1",
      netmask:"255.255.255.0",
      dns:"192.168.4.1",
      dhcp_enabled:1,
      site_id:"field-a",
      topic_prefix:"site/field-a",
      mqtt_port:1883,
      p2p_port:4884,
      broker_enabled:1,
      bridge_enabled:1,
      mesh_enabled:1
    }')
http_json POST "$ESP32_HTTP/config" "$TOKEN" "$CONFIG" | tee "$LOG_DIR/esp32-config-response.json"

printf '\n[5/6] Start Linux Wi-Fi hotspot: %s\n' "$LINUX_AP_SSID"
nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
nmcli device wifi hotspot ifname "$WIFI_IFACE" ssid "$LINUX_AP_SSID" password "$LINUX_AP_PASS"
HOTSPOT_CONN=$(nmcli -g GENERAL.CONNECTION device show "$WIFI_IFACE")
nmcli connection modify "$HOTSPOT_CONN" 802-11-wireless.band bg 802-11-wireless.channel "$LINUX_AP_CHANNEL"
nmcli connection up "$HOTSPOT_CONN" ifname "$WIFI_IFACE" >/dev/null
nmcli device show "$WIFI_IFACE" | tee "$LOG_DIR/linux-hotspot-device.txt"
if [ "$RESET_ESP32_AFTER_HOTSPOT" = "1" ]; then
    printf '\n[5b/6] Reset ESP32 with Linux hotspot already running\n'
    SKIP_BUILD=1 LOG_SECONDS=12 "$ROOT_DIR/scripts/hw_esp32_flash_monitor.sh"
fi

printf '\n[6/6] Wait for ESP32 to join Linux hotspot and answer HTTP\n'
ESP32_STA_IP=$(find_esp_on_linux_ap "$WIFI_IFACE" || true)
if [ -z "$ESP32_STA_IP" ]; then
    printf 'error: ESP32 did not appear on Linux hotspot within %ss\n' "$WAIT_SECONDS" >&2
    printf 'hint: current firmware may still only project Wi-Fi state in runtime and not perform real STA join.\n' >&2
    exit 1
fi

printf 'ESP32 station IP: %s\n' "$ESP32_STA_IP"
http_json GET "http://$ESP32_STA_IP:8080/status" "$TOKEN" | tee "$LOG_DIR/esp32-status-on-linux-ap.json"

printf '\nHardware Wi-Fi bridge test passed. Logs are in %s\n' "$LOG_DIR"
