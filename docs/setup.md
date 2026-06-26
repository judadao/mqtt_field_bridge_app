# Setup

Use the setup wrapper for the common path:

```sh
./scripts/setup.sh
```

By default it downloads the pinned dependencies into `deps/` and runs the Linux
unit tests.

## Options

```sh
# Use sibling local module checkouts instead of pinned git downloads
./scripts/setup.sh --local

# Sync dependencies only
./scripts/setup.sh --deps-only

# Also run the main Linux validation suite
./scripts/setup.sh --full-test

# Also build the default Ethernet ESP32 firmware
./scripts/setup.sh --build

# Local module development plus firmware build
./scripts/setup.sh --local --build
```

## Dependency Commands

The wrapper calls `scripts/sync_deps.sh`. Direct commands are still available
when you need finer control:

```sh
# Clone/fetch pinned deps into deps/ (default command)
./scripts/sync_deps.sh download

# Initialize Zephyr modules through the pinned Dephy workspace
./scripts/sync_deps.sh init

# Replace deps/ from sibling local checkouts
./scripts/sync_deps.sh replace

# Check whether the pinned broker tag is current
./scripts/sync_deps.sh --check-latest
```

`local-build` runs `replace` and then builds. `external-build` runs `download`,
`init`, and then builds.

## Firmware Build

Build the default Ethernet product firmware:

```sh
./scripts/sync_deps.sh external-build
```

During local module development:

```sh
./scripts/sync_deps.sh local-build
```

The board is read from `deps.json`; the current default is
`esp32_devkitc/esp32/procpu`. The default build uses `app/prj.conf` plus the
Dephy ESP32 slim product config listed in `deps.json`.

The WiFi/Linux AP profile is a test profile, not the default product path:

```sh
./scripts/build_wifi_bridge_product.sh
```

## Linux Tests

```sh
# Unit tests only
make -C tests/linux unit-tests

# Broker/P2P integration scenarios
make -C tests/linux integration-tests

# Reconnect and throughput stress tests
make -C tests/linux stress

# Main local validation entry point via dephy_testkit wrappers
make -C tests/linux test
```

Additional scale, hardware, and benchmark targets are documented in
`tests/linux/README.md`.

## Optional Linux Web Helper

For browser inspection of the compatibility web/config helper:

```sh
./run_linux_web.sh
```

Open `http://127.0.0.1:8080/`. This helper is useful for local Linux checks, but
it is not the primary firmware provisioning path.
