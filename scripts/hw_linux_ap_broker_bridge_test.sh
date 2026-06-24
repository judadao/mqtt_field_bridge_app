#!/usr/bin/env bash
# Linux-AP-focused bridge test:
# - keep existing Ethernet untouched
# - start a Linux Wi-Fi hotspot for ESP32 STA
# - start the Linux MQTT/P2P broker
# - optionally reset/flash ESP32 and capture UART logs
# - wait for ESP32 to join the Linux AP and expose HTTP/MQTT/P2P ports
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG_DIR=${LOG_DIR:-"$ROOT_DIR/tests/linux/out/hardware"}
WIFI_IFACE=${WIFI_IFACE:-}
AP_WIFI_IFACE=${AP_WIFI_IFACE:-}
ESP32_WIFI_IFACE=${ESP32_WIFI_IFACE:-}
LINUX_AP_SSID=${LINUX_AP_SSID:-Linux-Bridge-Test}
LINUX_AP_PASS=${LINUX_AP_PASS:-bridge1234}
LINUX_AP_CHANNEL=${LINUX_AP_CHANNEL:-6}
LINUX_AP_CONN=${LINUX_AP_CONN:-Linux-Bridge-Test-ap}
LINUX_AP_ADDR=${LINUX_AP_ADDR:-auto}
WAIT_SECONDS=${WAIT_SECONDS:-120}
RESET_ESP32=${RESET_ESP32:-0}
START_BROKER=${START_BROKER:-1}
KEEP_AP=${KEEP_AP:-1}
KEEP_BROKER=${KEEP_BROKER:-1}
WAIT_ESP32=${WAIT_ESP32:-1}
BROKER_PORT=${BROKER_PORT:-1883}
P2P_PORT=${P2P_PORT:-4884}

mkdir -p "$LOG_DIR"
STAMP=$(date +%Y%m%d-%H%M%S)
RUN_LOG="$LOG_DIR/linux-ap-bridge-$STAMP.log"
BROKER_LOG="$LOG_DIR/linux-broker-$STAMP.log"
exec > >(tee "$RUN_LOG") 2>&1

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'error: missing command: %s\n' "$1" >&2
        exit 1
    }
}

need curl
need ip
need nmcli
need rg
need ss

detect_wifi_iface() {
    if [ -n "$WIFI_IFACE" ]; then
        printf '%s\n' "$WIFI_IFACE"
        return 0
    fi
    if [ -n "$AP_WIFI_IFACE" ]; then
        printf '%s\n' "$AP_WIFI_IFACE"
        return 0
    fi
    nmcli -t -f DEVICE,TYPE device status |
        awk -F: -v sta="$ESP32_WIFI_IFACE" '$2 == "wifi" && $1 != sta { print $1; exit }'
}

cleanup() {
    if [ "${BROKER_PID:-}" ] && [ "$KEEP_BROKER" != "1" ]; then
        kill "$BROKER_PID" >/dev/null 2>&1 || true
        wait "$BROKER_PID" >/dev/null 2>&1 || true
    fi
    if [ "$KEEP_AP" != "1" ] && [ -n "${HOTSPOT_CONN:-}" ]; then
        nmcli connection down "$HOTSPOT_CONN" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

wait_tcp() {
    host=$1
    port=$2
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        if timeout 3 bash -c "</dev/tcp/$host/$port" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    return 1
}

find_esp_on_linux_ap() {
    iface=$1
    gateway=${LINUX_AP_ADDR%/*}
    deadline=$((SECONDS + WAIT_SECONDS))
    while [ "$SECONDS" -lt "$deadline" ]; do
        while read -r ipaddr _; do
            [ -n "$ipaddr" ] || continue
            case "$ipaddr" in
                "$gateway"|169.254.*) continue ;;
            esac
            if curl -fsS --max-time 4 "http://$ipaddr:8080/status" >/dev/null 2>&1; then
                printf '%s\n' "$ipaddr"
                return 0
            fi
        done < <(ip -4 neigh show dev "$iface" | awk '{print $1, $0}')
        sleep 1
    done
    return 1
}

choose_linux_ap_addr() {
    local third gateway

    if [ "$LINUX_AP_ADDR" != "auto" ]; then
        printf '%s\n' "$LINUX_AP_ADDR"
        return 0
    fi

    for third in $(seq 77 99); do
        gateway="10.$third.0.1"
        if ! ss -ltnup 2>/dev/null | rg --fixed-strings "$gateway:" >/dev/null &&
           ! ip -4 route show 2>/dev/null | rg --fixed-strings "10.$third.0.0/24" >/dev/null; then
            printf '%s/24\n' "$gateway"
            return 0
        fi
    done

    printf 'error: no free 10.77.0.1-10.99.0.1 AP subnet found\n' >&2
    return 1
}

WIFI_IFACE=$(detect_wifi_iface)
[ -n "$WIFI_IFACE" ] || {
    printf 'error: no Wi-Fi interface found by NetworkManager\n' >&2
    exit 1
}
LINUX_AP_ADDR=$(choose_linux_ap_addr)

printf 'Wi-Fi interface: %s\n' "$WIFI_IFACE"
printf 'ESP32 Wi-Fi if:  %s\n' "${ESP32_WIFI_IFACE:-auto/unused}"
printf 'Linux AP SSID:   %s\n' "$LINUX_AP_SSID"
printf 'Linux AP pass:   %s\n' "$LINUX_AP_PASS"
printf 'Linux AP addr:   %s\n' "$LINUX_AP_ADDR"
printf 'Start broker:    %s\n' "$START_BROKER"
printf 'Wait ESP32:      %s\n' "$WAIT_ESP32"
printf 'Run log:         %s\n' "$RUN_LOG"
printf 'Broker log:      %s\n' "$BROKER_LOG"

if [ "$START_BROKER" = "1" ]; then
    printf '\n[1/5] Build and start Linux MQTT/P2P broker\n'
    make -C "$ROOT_DIR/deps/mqtt_min_broker" -f Makefile.linux \
        P2P=1 MQTT_PORT="$BROKER_PORT" P2P_PORT="$P2P_PORT" all
    "$ROOT_DIR/deps/mqtt_min_broker/build_out/mqtt_broker" >"$BROKER_LOG" 2>&1 &
    BROKER_PID=$!
    printf '%s\n' "$BROKER_PID" >"$LOG_DIR/linux-broker.pid"
    wait_tcp 127.0.0.1 "$BROKER_PORT" || {
        printf 'error: Linux broker did not listen on %s\n' "$BROKER_PORT" >&2
        exit 1
    }
    printf 'Linux broker PID: %s\n' "$BROKER_PID"
    if [ "$KEEP_BROKER" = "1" ]; then
        printf 'Linux broker will stay running after this script exits.\n'
    fi
else
    printf '\n[1/5] Skip Linux broker start (START_BROKER=0)\n'
fi

printf '\n[2/5] Start Linux Wi-Fi AP for ESP32 STA\n'
nmcli radio wifi on
nmcli device disconnect "$WIFI_IFACE" >/dev/null 2>&1 || true
if [ -f "/run/nm-dnsmasq-$WIFI_IFACE.pid" ]; then
    DNSMASQ_PID=$(cat "/run/nm-dnsmasq-$WIFI_IFACE.pid" 2>/dev/null || true)
    if [ -n "$DNSMASQ_PID" ]; then
        kill "$DNSMASQ_PID" >/dev/null 2>&1 || true
    fi
    rm -f "/run/nm-dnsmasq-$WIFI_IFACE.pid" >/dev/null 2>&1 || true
fi
pkill -f "dnsmasq.*dnsmasq-$WIFI_IFACE\\.leases" >/dev/null 2>&1 || true
pkill -f "dnsmasq.*nm-dnsmasq-$WIFI_IFACE\\.pid" >/dev/null 2>&1 || true
nmcli connection down "$LINUX_AP_CONN" >/dev/null 2>&1 || true
nmcli connection delete "$LINUX_AP_CONN" >/dev/null 2>&1 || true
nmcli connection add type wifi ifname "$WIFI_IFACE" \
    con-name "$LINUX_AP_CONN" ssid "$LINUX_AP_SSID" >/dev/null
nmcli connection modify "$LINUX_AP_CONN" \
    connection.autoconnect no \
    802-11-wireless.mode ap \
    802-11-wireless.band bg \
    802-11-wireless.channel "$LINUX_AP_CHANNEL" \
    wifi-sec.key-mgmt wpa-psk \
    wifi-sec.psk "$LINUX_AP_PASS" \
    ipv4.method shared \
    ipv4.addresses "$LINUX_AP_ADDR" \
    ipv6.method ignore
nmcli connection up "$LINUX_AP_CONN" ifname "$WIFI_IFACE" >/dev/null
HOTSPOT_CONN=$LINUX_AP_CONN
nmcli device show "$WIFI_IFACE" | tee "$LOG_DIR/linux-ap-device-$STAMP.txt"

printf '\n[3/5] ESP32 configuration expectation\n'
printf 'Configure ESP32 STA from its web UI or persisted settings:\n'
printf '  wifi_ssid=%s\n' "$LINUX_AP_SSID"
printf '  wifi_password=%s\n' "$LINUX_AP_PASS"
printf '  Linux broker host=%s mqtt=%s p2p=%s\n' "${LINUX_AP_ADDR%/*}" "$BROKER_PORT" "$P2P_PORT"
printf 'ESP32 may keep its own AP enabled for your second laptop.\n'

if [ "$WAIT_ESP32" != "1" ]; then
    printf '\nLinux AP + broker environment is up.\n'
    printf 'SSID:     %s\n' "$LINUX_AP_SSID"
    printf 'Password: %s\n' "$LINUX_AP_PASS"
    printf 'Broker:   %s:%s\n' "${LINUX_AP_ADDR%/*}" "$BROKER_PORT"
    exit 0
fi

if [ "$RESET_ESP32" = "1" ]; then
    printf '\n[4/5] Reset/flash ESP32 and capture UART log\n'
    SKIP_BUILD=1 LOG_SECONDS=30 "$ROOT_DIR/scripts/hw_esp32_flash_monitor.sh"
else
    printf '\n[4/5] Skip ESP32 reset (RESET_ESP32=0)\n'
fi

printf '\n[5/5] Wait for ESP32 to join Linux AP and expose bridge services\n'
ESP32_STA_IP=$(find_esp_on_linux_ap "$WIFI_IFACE" || true)
if [ -z "$ESP32_STA_IP" ]; then
    printf 'error: ESP32 did not answer HTTP on Linux AP within %ss\n' "$WAIT_SECONDS" >&2
    printf 'Current neighbor table:\n'
    ip -4 neigh show dev "$WIFI_IFACE" || true
    exit 1
fi

printf 'ESP32 station IP: %s\n' "$ESP32_STA_IP"
curl -fsS --max-time 8 "http://$ESP32_STA_IP:8080/status" |
    tee "$LOG_DIR/esp32-status-on-linux-ap-$STAMP.json"

wait_tcp "$ESP32_STA_IP" 1883 || {
    printf 'error: ESP32 MQTT port 1883 did not open\n' >&2
    exit 1
}
wait_tcp "$ESP32_STA_IP" 4884 || {
    printf 'error: ESP32 P2P port 4884 did not open\n' >&2
    exit 1
}

printf '\nLinux AP + broker bridge test environment is up.\n'
printf 'Linux AP gateway/broker: %s\n' "${LINUX_AP_ADDR%/*}"
printf 'ESP32 STA:              %s\n' "$ESP32_STA_IP"
