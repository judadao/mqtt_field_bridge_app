#!/usr/bin/env bash
# Install host-side tools used by ESP32 Ethernet validation tests.
set -euo pipefail

if ! command -v sudo >/dev/null 2>&1; then
    printf 'error: sudo is required to install host network test tools\n' >&2
    exit 1
fi

sudo apt-get update
sudo apt-get install -y iputils-arping tcpdump libcap2-bin

TCPDUMP=$(command -v tcpdump)
sudo setcap cap_net_raw,cap_net_admin=eip "$TCPDUMP"

printf 'installed: %s\n' "$(command -v arping)"
printf 'configured: %s %s\n' "$TCPDUMP" "$(getcap "$TCPDUMP" || true)"
