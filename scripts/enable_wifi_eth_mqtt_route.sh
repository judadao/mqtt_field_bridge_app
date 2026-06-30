#!/usr/bin/env bash
# Enable narrowly scoped routing from the Linux AP network to ETH ESP32 brokers.
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ACTION=${1:-enable}

WIFI_IFACE=${WIFI_IFACE:-wlx3c64cf742c7b}
ETH_IFACE=${ETH_IFACE:-enx34298f70f788}
WIFI_CIDR=${WIFI_CIDR:-10.88.0.0/24}
BROKERS=${BROKERS:-"192.168.127.15 192.168.127.16 192.168.127.17 192.168.127.18 192.168.127.19"}
WIFI_BROKER=${WIFI_BROKER:-10.88.0.2}
PORTS=${PORTS:-1883,4884}
STATE_DIR=${STATE_DIR:-"$ROOT_DIR/tests/linux/out/wifi-eth-route"}

mkdir -p "$STATE_DIR"

need_root() {
    if [ "$(id -u)" -ne 0 ]; then
        exec sudo -- "$0" "$ACTION"
    fi
}

backup_state() {
    if [ ! -f "$STATE_DIR/ip_forward.before" ]; then
        cat /proc/sys/net/ipv4/ip_forward >"$STATE_DIR/ip_forward.before"
    fi
    if [ ! -f "$STATE_DIR/iptables.before" ]; then
        iptables-save >"$STATE_DIR/iptables.before"
    fi
}

append_once() {
    chain=$1
    shift
    if ! iptables -C "$chain" "$@" >/dev/null 2>&1; then
        iptables -A "$chain" "$@"
    fi
}

append_nat_once() {
    chain=$1
    shift
    if ! iptables -t nat -C "$chain" "$@" >/dev/null 2>&1; then
        iptables -t nat -A "$chain" "$@"
    fi
}

enable_rules() {
    need_root
    backup_state
    sysctl -w net.ipv4.ip_forward=1 >/dev/null

    for broker in $BROKERS; do
        append_once FORWARD -i "$WIFI_IFACE" -o "$ETH_IFACE" \
            -s "$WIFI_CIDR" -d "$broker/32" -p tcp \
            -m multiport --dports "$PORTS" -j ACCEPT
        append_once FORWARD -i "$ETH_IFACE" -o "$WIFI_IFACE" \
            -s "$broker/32" -d "$WIFI_CIDR" -p tcp \
            -m multiport --sports "$PORTS" \
            -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
        append_once FORWARD -i "$ETH_IFACE" -o "$WIFI_IFACE" \
            -s "$broker/32" -d "$WIFI_BROKER/32" -p tcp \
            -m multiport --dports "$PORTS" -j ACCEPT
        append_once FORWARD -i "$WIFI_IFACE" -o "$ETH_IFACE" \
            -s "$WIFI_BROKER/32" -d "$broker/32" -p tcp \
            -m multiport --sports "$PORTS" \
            -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
        append_nat_once POSTROUTING -o "$ETH_IFACE" \
            -s "$WIFI_CIDR" -d "$broker/32" -p tcp \
            -m multiport --dports "$PORTS" -j MASQUERADE
        append_nat_once POSTROUTING -o "$WIFI_IFACE" \
            -s "$broker/32" -d "$WIFI_BROKER/32" -p tcp \
            -m multiport --dports "$PORTS" -j MASQUERADE
    done

    printf 'enabled WiFi AP -> ETH ESP MQTT/P2P route rules\n'
    printf 'backup: %s\n' "$STATE_DIR"
}

restore_rules() {
    need_root
    if [ ! -f "$STATE_DIR/iptables.before" ] || [ ! -f "$STATE_DIR/ip_forward.before" ]; then
        printf 'error: no saved state in %s\n' "$STATE_DIR" >&2
        exit 1
    fi
    iptables-restore <"$STATE_DIR/iptables.before"
    sysctl -w "net.ipv4.ip_forward=$(cat "$STATE_DIR/ip_forward.before")" >/dev/null
    printf 'restored iptables and ip_forward from %s\n' "$STATE_DIR"
}

status_rules() {
    need_root
    printf 'ip_forward=%s\n' "$(cat /proc/sys/net/ipv4/ip_forward)"
    printf '\nFORWARD rules:\n'
    iptables -S FORWARD | grep -E "$WIFI_IFACE|$ETH_IFACE|192\\.168\\.127\\.(15|16|17|18|19)" || true
    printf '\nPOSTROUTING rules:\n'
    iptables -t nat -S POSTROUTING | grep -E "$WIFI_IFACE|$ETH_IFACE|192\\.168\\.127\\.(15|16|17|18|19)" || true
}

case "$ACTION" in
    enable) enable_rules ;;
    restore) restore_rules ;;
    status) status_rules ;;
    *)
        printf 'usage: %s [enable|restore|status]\n' "$0" >&2
        exit 2
        ;;
esac
