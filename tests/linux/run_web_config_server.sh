#!/usr/bin/env bash
# Starts the Linux provisioning web UI with optional local static network values.
set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ENV_FILE=${WEB_NETWORK_ENV:-"$DIR/local_web_network.env"}

if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ENV_FILE"
fi

export WEB_TEST_DEVICE_IP="${WEB_TEST_DEVICE_IP:-}"
export WEB_TEST_GATEWAY="${WEB_TEST_GATEWAY:-}"
export WEB_TEST_NETMASK="${WEB_TEST_NETMASK:-}"
export WEB_TEST_DNS="${WEB_TEST_DNS:-}"
export WEB_TEST_DHCP_ENABLED="${WEB_TEST_DHCP_ENABLED:-0}"

mkdir -p "$DIR/out"

make -C "$DIR" out/run_web_config_server >/dev/null

cd "$DIR"
exec "$DIR/out/run_web_config_server"
