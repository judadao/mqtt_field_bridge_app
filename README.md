# mqtt_field_bridge_app

Product application for configurable MQTT field bridge deployments.

`mqtt_field_bridge_app` is the product layer that turns reusable modules into a
deployable field bridge. It owns product configuration, provisioning HTTP/UI,
bridge peer workflow, integration tests, and dependency pins. Broker, board,
industrial IO, and test harness behavior are implemented in module repos first
and consumed here.

## What It Provides

- A product composition point for MQTT broker, field IO, board profile, and test
  harness modules.
- Provisioning flow for network and bridge settings.
- Field bridge runtime that can route local IO state into MQTT topics.
- Linux tests for product config, dependency sync, provisioning rendering, and
  integration behavior.
- A clear split between product workflow and reusable module logic.

## Normal Flow

1. Sync pinned dependencies from `deps.json`.
2. For local development, replace synced dependencies with sibling checkouts.
3. Run Linux tests to validate product behavior without hardware.
4. Build the Zephyr product app.
5. Validate remaining WiFi and IP behavior on ESP32 hardware.

Commands:

```sh
./scripts/sync_deps.sh download
./scripts/sync_deps.sh replace
./scripts/sync_deps.sh init
./scripts/build_product.sh

make -C tests/linux unit-tests
make -C tests/linux unit-tests provisioning-render-size testkit-wrapper
make -C tests/linux test
```

## How It Works

The product repo stays thin by composing pinned modules:

- `dephy` provides the ESP32 board profile and Zephyr workspace setup.
- `mqtt_min_broker` provides broker behavior and optional P2P routing.
- `dephy_industrial_io` provides IO channel state and MQTT payload helpers.
- `dephy_testkit` provides Linux fixtures and structured test results.

Product code should include public module headers and call module entry points.
If logic would be useful to another product, implement it in the module first,
tag the module, then bump this product's `deps.json`.

## Layout

```text
app/                 product Zephyr app, runtime, provisioning, UI assets
deps.json            pinned reusable modules
scripts/             dependency sync and product build commands
tests/linux/         Linux unit, integration, browser, stress tests
docs/                design notes, validation notes, legacy README
docs/todo.yaml       product TODO source of truth
```

## Current TODO

Only hardware-blocked items remain in `docs/todo.yaml`:

- ESP32 peer reconnection after WiFi reconnect.
- Live ESP32 WiFi scan integration.
- DHCP-derived STA/AP/peer broker IP role validation.

## More Docs

- `docs/readme_legacy.md`: previous long README with detailed workflows and API
  examples.
- `docs/field_bridge_scenario.md`: field scenario notes.
- `docs/field_validation_checklist.md`: hardware validation checklist.
- `docs/memory.md`: memory notes.
- `docs/versioning.md`: release/version notes.
