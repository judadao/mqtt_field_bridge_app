#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

MODE=download
RUN_UNIT=1
RUN_FULL_TESTS=0
RUN_BUILD=0

usage() {
    cat <<'EOF'
usage: scripts/setup.sh [options]

Default:
  Download pinned dependencies into deps/ and run Linux unit tests.

Options:
  --local       replace deps/ from sibling local module checkouts
  --build       also build the default Ethernet ESP32 firmware
  --full-test   also run the main Linux validation suite
  --deps-only   sync dependencies only
  -h, --help    show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --local)
            MODE=replace
            ;;
        --build)
            RUN_BUILD=1
            ;;
        --full-test)
            RUN_FULL_TESTS=1
            ;;
        --deps-only)
            RUN_UNIT=0
            RUN_FULL_TESTS=0
            RUN_BUILD=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'error: unknown option: %s\n\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'error: %s is required\n' "$1" >&2
        exit 1
    fi
}

need git
need make
need gcc

cd "$ROOT_DIR"

if [ "$MODE" = "replace" ]; then
    printf '==> Replacing deps/ from sibling local checkouts\n'
    ./scripts/sync_deps.sh replace
else
    printf '==> Downloading pinned dependencies into deps/\n'
    ./scripts/sync_deps.sh download
fi

if [ "$RUN_UNIT" -eq 1 ]; then
    printf '\n==> Running Linux unit tests\n'
    make -C tests/linux unit-tests
fi

if [ "$RUN_FULL_TESTS" -eq 1 ]; then
    printf '\n==> Running main Linux validation suite\n'
    make -C tests/linux test
fi

if [ "$RUN_BUILD" -eq 1 ]; then
    printf '\n==> Building default Ethernet ESP32 firmware\n'
    if [ "$MODE" = "replace" ]; then
        ./scripts/build_product.sh
    else
        ./scripts/sync_deps.sh init
        ./scripts/build_product.sh
    fi
fi

printf '\nSetup complete.\n'
