# mqtt_field_bridge_app

Product application for configurable MQTT field bridge deployments.

This repo is the product layer. It owns field configuration, provisioning HTTP
and UI, bridge peer workflow, product tests, and dependency pins. Reusable
broker, board, IO, and test harness behavior belongs in module repos first.

## Dependencies

Pinned in `deps.json` and materialized under `deps/`:

- `dephy`: board profile and Zephyr workspace setup.
- `mqtt_min_broker`: MQTT broker and optional P2P routing.
- `dephy_industrial_io`: industrial IO topic/payload bridge helpers.
- `dephy_testkit`: Linux test harness and JSON result wrapper.

## Layout

```text
app/                 product Zephyr app, runtime, provisioning, UI assets
deps.json            pinned reusable modules
scripts/             dependency sync and product build commands
tests/linux/         Linux unit, integration, browser, stress tests
docs/                design notes, validation notes, legacy README
docs/todo.yaml       product TODO source of truth
```

## Quick Commands

```sh
./scripts/sync_deps.sh download
./scripts/sync_deps.sh replace
./scripts/sync_deps.sh init
./scripts/build_product.sh

make -C tests/linux unit-tests
make -C tests/linux unit-tests provisioning-render-size testkit-wrapper
sh tests/linux/test_sync_deps.sh
```

Full Linux suite:

```sh
make -C tests/linux test
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
