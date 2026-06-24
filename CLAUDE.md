# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# 1. Sync pinned dependencies into deps/
./scripts/sync_deps.sh

# 2. Build for ESP32 (board read from deps.json)
./scripts/build_product.sh
```

`build_product.sh` uses `deps/dephy/zephyrproject` when available and passes every dependency module path from `deps.json` through `ZEPHYR_EXTRA_MODULES`. The current Zephyr target is `esp32_devkitc/esp32/procpu`. Zephyr ≥ v3.5.0 is required.

## Toolchain Setup (one-time)

`west` is not in PATH by default. The broker repo provides an isolated setup script:

```bash
# Install Zephyr SDK + west into ~/zephyrproject (isolated venv, no ~/.bashrc changes)
cd /home/judd/moxa/personal/mqtt_min_broker
./install_env.sh

# Activate before each build session
source /home/judd/moxa/personal/mqtt_min_broker/env.sh
```

After sourcing `env.sh`, `west` and the ESP32 cross-compiler are in PATH and `./scripts/build_product.sh` works.

## Linux Build (dev/test — no hardware required)

The broker dep builds on Linux. Product logic (product_config, bridge_control,
provisioning_http) can be tested without Zephyr using `tests/linux/`:

```bash
make -C tests/linux all   # build Linux test binaries
make -C tests/linux test  # run all Linux tests
```

See `tests/linux/README.md` for per-test instructions and stress test knobs.

## Architecture

This is a **Zephyr RTOS application** targeting ESP32. It is the product layer on top of pinned modules materialized under `deps/`, including `deps/mqtt_min_broker` and the Dephy board profile at `deps/dephy/boards/esp32`.

**Startup order** (`main.c`):
1. `product_config_init()` — initializes settings/peer config and loads Linux file or Zephyr NVS persistence
2. `product_runtime_init()` / `product_runtime_network_start()` — initializes product runtime status from saved network settings
3. `bridge_control_init()` — applies enabled peers through `bridge_control_apply_peers()`
4. `provisioning_http_start()` — starts the product-owned HTTP server for settings, status, peer config, broker control, and topic test
5. `client_pool_init()` / `broker_init()` / `p2p_start()` / `broker_run()` — start the embedded broker from the dep when broker config is enabled

`CONFIG_MQTT_STANDALONE=n` in `prj.conf` disables the broker's own `main()` so the product app owns the entry point.

**Module boundaries:**
- `product_config` — owns validated system/network/broker settings, peer table, reset path, and persistence
- `product_runtime` — owns runtime status projection, broker desired state, and product-level publish-test records
- `bridge_control` — validates enabled peer config and is the product integration point for broker/P2P peer application
- `provisioning_http` — HTTP server and firmware-served HTML UI for System/Network/Broker settings, peer config, broker control, reset, and Topic Test
- `deps/mqtt_min_broker` — broker implementation; treat as read-only from this repo

## Dependency Rule

Never make permanent patches to `deps/mqtt_min_broker`. If a broker fix is needed: open an issue in the broker repo, fix and tag there (`minmqtt-vX.Y.Z`), then bump `deps.json` here. Temporary local patches must be upstreamed before merging.

## Tag Namespaces

- Product releases: `bridge-vX.Y.Z`
- Broker dependency: `minmqtt-vX.Y.Z` (referenced in `deps.json`)

## Field Scenario

Three ESP32 nodes (Note 1/2/3). Note 1 owns 4510 data and publishes on `site/<site_id>/4510/<stream>`. Notes 2 and 3 subscribe through the P2P mesh. The product app must enforce startup ordering: network → broker → P2P.
