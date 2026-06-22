#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

need()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'error: %s is required\n' "$1" >&2
        exit 1
    fi
}

all_local_deps_exist()
{
    [ -d "$ROOT_DIR/../mqtt_min_broker" ] &&
    [ -d "$ROOT_DIR/../dephy" ] &&
    [ -d "$ROOT_DIR/../dephy_industrial_io" ] &&
    [ -d "$ROOT_DIR/../dephy_testkit" ]
}

need git
need make
need gcc
need node

if [ ! -d "$ROOT_DIR/deps/mqtt_min_broker" ] ||
   [ ! -d "$ROOT_DIR/deps/dephy_industrial_io" ]; then
    if all_local_deps_exist; then
        printf 'Using sibling module checkouts from ../*\n'
        "$ROOT_DIR/scripts/sync_deps.sh" replace
    else
        printf 'Downloading pinned dependencies into deps/\n'
        "$ROOT_DIR/scripts/sync_deps.sh" download
    fi
fi

printf '\nStarting Linux provisioning UI...\n'
printf 'Open: http://127.0.0.1:8080/\n'
printf 'Login password: admin\n\n'

exec "$ROOT_DIR/tests/linux/run_web_config_server.sh"
