# mqtt_field_bridge_app

Product application for the Note 1 / Note 2 / Note 3 field bridge scenario.

This repository consumes `mqtt_min_broker` as a pinned dependency under
`deps/mqtt_min_broker`. Product builds should use the broker version declared in
`deps.json`, not a floating local checkout.

## Repo Layout

```text
mqtt_field_bridge_app/
├── deps.json
├── deps/
│   └── mqtt_min_broker/
├── app/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── src/
├── scripts/
│   ├── sync_deps.sh
│   └── build_product.sh
└── docs/
    └── field_bridge_scenario.md
```

## Build Flow

1. Sync dependencies:

   ```bash
   ./scripts/sync_deps.sh
   ```

2. Build the product app:

   ```bash
   ./scripts/build_product.sh
   ```

The product app owns WiFi provisioning, local HTML UI, bridge peer
configuration, and the 4510 field workflow. The broker implementation remains
inside `deps/mqtt_min_broker`.

## Current Status

Implemented in this product repo:

- Pinned broker dependency sync through `deps.json` and `scripts/sync_deps.sh`.
- Peer config storage for three bridge peers, with Linux file persistence and
  Zephyr NVS persistence path.
- Bridge peer apply logic that validates enabled peers before handing them to
  the broker/P2P layer.
- Product-owned provisioning HTTP server with `/status`, `/peers`, and
  `POST /peers/<index>` endpoints.
- Linux unit, integration, reconnect stress, and throughput stress tests under
  `tests/linux/`.

Still open:

- Product network startup and board overlays for target ESP32 hardware.
- WiFi setup, broker control, publish-test endpoints, and the HTML UI.
- Full device/site config schema for role/name, WiFi, `site_id`, and 4510 topic
  prefix.
- Hardware validation logs and manual field checklist for Note 1/2/3.

## Dependency Rule

Update `deps.json` only after `mqtt_min_broker` has a new released tag.
Temporary fixes under `deps/mqtt_min_broker` should be upstreamed to the broker
repo and replaced by a tag bump.

## Product TODO

The execution TODO list is in [`docs/todo.md`](docs/todo.md).

Version and tag naming rules are in [`docs/versioning.md`](docs/versioning.md).
