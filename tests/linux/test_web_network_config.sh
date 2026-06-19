#!/usr/bin/env bash
# Runs the provisioning HTTP web/config unit test with local network values.
set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ENV_FILE=${WEB_NETWORK_ENV:-"$DIR/local_web_network.env"}

if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ENV_FILE"
fi

: "${WEB_TEST_DEVICE_IP:?set WEB_TEST_DEVICE_IP or create tests/linux/local_web_network.env}"
: "${WEB_TEST_GATEWAY:?set WEB_TEST_GATEWAY or create tests/linux/local_web_network.env}"
: "${WEB_TEST_NETMASK:?set WEB_TEST_NETMASK or create tests/linux/local_web_network.env}"
: "${WEB_TEST_DNS:?set WEB_TEST_DNS or create tests/linux/local_web_network.env}"

export WEB_TEST_REQUIRE=1
export WEB_TEST_DEVICE_IP
export WEB_TEST_GATEWAY
export WEB_TEST_NETMASK
export WEB_TEST_DNS

if [ ! -x "$DIR/out/unit_provisioning_http" ]; then
    make -C "$DIR" unit_provisioning_http >/dev/null
fi

"$DIR/out/unit_provisioning_http"
