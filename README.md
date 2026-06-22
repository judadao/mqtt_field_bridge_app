# mqtt_field_bridge_app

Product application for configurable MQTT field bridge deployments.

## Overview

`mqtt_field_bridge_app` composes reusable modules into a deployable field bridge.
It owns product configuration, provisioning HTTP/UI, bridge workflow, product
tests, and dependency pins.

## Key Value

- Product-level integration of broker, board profile, IO, and testkit modules.
- Provisioning flow for network and bridge settings.
- Linux validation before ESP32 hardware validation.
- Thin product layer that keeps reusable logic in module repos.

## How To Use

```sh
./scripts/sync_deps.sh download
./scripts/sync_deps.sh replace
./scripts/sync_deps.sh init
./scripts/build_product.sh
make -C tests/linux test
```

## Simple Principle

This repo composes modules and owns product workflow. Broker, board, IO, and
test harness logic should be fixed in their module repos first, then pinned here.

## Docs

- `docs/readme_legacy.md`: previous long README and detailed examples.
- `docs/field_bridge_scenario.md`: field scenario notes.
- `docs/field_validation_checklist.md`: hardware validation checklist.
- `docs/memory.md`: memory notes.
- `docs/todo.md`: current TODO summary.
