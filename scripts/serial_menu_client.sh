#!/usr/bin/env bash
set -euo pipefail

serial_port="${1:?usage: serial_menu_client.sh /dev/ttyUSBx}"

stty -F "$serial_port" 115200 raw -echo -echoe -echok -echonl \
    -icanon -isig -iexten -ixon -ixoff -icrnl -inlcr -igncr \
    -opost -onlcr -hupcl clocal 2>/dev/null || true

printf 'menu\r' >"$serial_port"
exec socat - "FILE:${serial_port},b115200,raw,echo=0,clocal=1"
