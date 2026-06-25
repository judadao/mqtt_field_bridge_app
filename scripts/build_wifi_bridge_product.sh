#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build_wifi_bridge_product"}
EXTRA_CONF_FILES_APPEND=${EXTRA_CONF_FILES_APPEND:-"$ROOT_DIR/app/prj_wifi_linux_ap.conf"}

export BUILD_DIR
export EXTRA_CONF_FILES_APPEND

"$ROOT_DIR/scripts/build_product.sh"
