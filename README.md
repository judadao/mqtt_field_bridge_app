# mqtt_field_bridge_app

Product application for configurable MQTT field bridge scenarios.

This repository consumes `mqtt_min_broker` as a pinned dependency under
`deps/mqtt_min_broker`. Product builds should use the broker version declared in
`deps.json`, not a floating local checkout.

Current pinned broker release: `minmqtt-v0.1.12`.

## What This App Does

`mqtt_field_bridge_app` is the product layer around `mqtt_min_broker` for a
field-deployed MQTT mesh. The first validation setup uses three ESP32-class
nodes, but the product config is sized for larger peer lists:

- One or more edge nodes receive or produce field data.
- Multiple peer nodes subscribe to that data through broker-to-broker P2P routing.
- Each node keeps a local MQTT broker available, so local clients can continue
  to publish or subscribe even when another field node is temporarily offline.
- The product peer table defaults to 10 slots and can be raised at build time
  with `FIELD_BRIDGE_PEER_MAX` if the target has enough memory and flash.
- Broker nodes do not need to form a full mesh. If node 1 bridges to node 2 and
  node 3 bridges to node 2, all three are expected to behave as one connected
  broker network through node 2.

The broker dependency handles MQTT packet parsing, sessions, topic matching,
P2P discovery, and inter-node routing. This product app owns the field-specific
configuration, startup order, local provisioning/control endpoints, and topic
workflow decisions.

## Usage Scenarios

- Field telemetry bridge: publish field data on one node and route matching
  subscriptions to any reachable peer node over the P2P broker network.
- Local-first operation: keep local MQTT delivery working while peer nodes are
  offline, rebooting, or reconnecting.
- Site staging and provisioning: configure peer host/IP, MQTT port, P2P port,
  and enabled state through product-owned config APIs before field deployment.
- Broker dependency release validation: pin a broker tag in `deps.json`, sync it
  into `deps/mqtt_min_broker`, and run the Linux suite before building firmware.
- Hardware bring-up preparation: exercise product config, peer application, HTTP
  endpoints, and broker mesh behavior on Linux before moving to ESP32 boards.

## Why This Split Is Useful

- Reproducible builds: the broker version is pinned in `deps.json`, so product
  builds do not silently pick up whatever local broker checkout happens to exist.
- Clean ownership: product-specific WiFi setup, field workflow, and local UI
  stay here; reusable MQTT broker logic stays in `mqtt_min_broker`.
- Linux-first validation: most product behavior can be tested without hardware,
  including peer persistence, HTTP provisioning endpoints, three-node routing,
  reconnect recovery, and throughput stress.
- Safer broker upgrades: dependency sync refuses dirty broker checkouts and
  verifies that the pinned tag exists before checkout.
- Offline-tolerant field behavior: tests cover local delivery while a peer node
  is offline and recovery after peer restart.
- Scalable peer configuration: the local settings page and JSON API expose all
  configured peer slots, so 10-broker lab and field setups do not need a UI
  redesign or full-mesh peer wiring.

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

## Quick Start

1. Sync dependencies:

   ```bash
   ./scripts/sync_deps.sh
   ```

   Check whether a newer broker release is available:

   ```bash
   ./scripts/sync_deps.sh --check-latest
   ```

2. Build the product app:

   ```bash
   ./scripts/build_product.sh
   ```

3. Run Linux tests:

   ```bash
   make -C tests/linux test
   ```

The product app owns WiFi provisioning, local HTML UI, bridge peer
configuration, and the topic bridge workflow. The broker implementation remains
inside `deps/mqtt_min_broker`.

## Linux Development

Linux tests compile product modules with a small Zephyr logging shim. This is
the fastest path for day-to-day validation:

```bash
# Unit tests only
make -C tests/linux unit-tests

# Broker dependency sync and chain-topology routing scenario
make -C tests/linux integration-tests

# Reconnect and throughput stress tests
make -C tests/linux stress

# Everything
make -C tests/linux test
```

Useful knobs:

- `SETTLE_SEC=5`: P2P mesh settle time for integration and stress tests.
- `RESTART_COUNT=5`: number of B2 restart cycles in reconnect stress; set
  `RESTART_COUNT=10` for a longer run.
- `VERIFY_TIMEOUT_SEC=5`: max wait for post-restart messages.
- `PUB_COUNT=5`, `SUB_COUNT=3`, `DURATION=10`: throughput stress load.
- `MIN_THROUGHPUT_MSG=500`: minimum aggregate received messages required.

## Provisioning HTTP API

The current product HTTP server is intentionally small and focused on peer
configuration:

```bash
# Status
curl http://127.0.0.1:8080/status

# List configured peers
curl http://127.0.0.1:8080/peers

# Configure peer slot 0
curl -X POST http://127.0.0.1:8080/peers/0 \
  -H 'Content-Type: application/json' \
  -d '{"name":"node2","host":"192.168.10.12","mqtt_port":1883,"p2p_port":4884,"enabled":1}'
```

Peer slots default to 10 entries. Updating a peer persists the peer config and calls
`bridge_control_apply_peers()`.

The same server also serves a local settings page:

```text
http://<device-ip>:8080/
```

The page lists peer slots, edits host and port values, toggles enabled state,
and saves through the same JSON endpoints.

## Topic Model

The intended topic family is:

```text
site/<site_id>/<stream>
```

Planned streams:

- `status`: device and field status.
- `io`: field IO payloads.
- `event`: field events and alarms.

The Linux integration tests exercise the topic routing shape with topics such as
`site/field-a/data/io` and wildcard subscribers on `site/field-a/data/#`, using
a chain topology rather than a full mesh.

## Current Status

Implemented in this product repo:

- Pinned broker dependency sync through `deps.json` and `scripts/sync_deps.sh`.
- Broker dependency updated to `minmqtt-v0.1.12`, which includes parser-level
  malformed packet hardening.
- Linux stress tests rebuild broker binaries after dependency changes and keep
  reconnect cycles conservative by default; use `RESTART_COUNT=10` for a longer
  reconnect run.
- Peer config storage for 10 bridge peers by default, with Linux file persistence and
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
- Full device/site config schema for role/name, WiFi, `site_id`, and topic
  prefix.
- Hardware validation logs and manual field checklist for larger peer counts.

## Latest Test Result

Last verified locally on 2026-06-19 with:

```bash
make -C tests/linux test
```

Result:

- `unit_product_config`: 161/161 checks passed.
- `unit_bridge_control`: 7/7 tests passed.
- `unit_provisioning_http`: 32/32 checks passed.
- `test_sync_deps.sh`: 11 passed, 0 failed.
- `test_3node_scenario.sh`: 4 passed, 0 failed.
- `stress_reconnect.sh`: 5 restart cycles passed; B1 survived all cycles.
- `stress_throughput.sh`: 2,330,804 messages received in 10 seconds
  (`233,080 msg/s`), above the 500-message minimum; all three brokers survived.

## Release And Tagging

Product releases use `bridge-vX.Y.Z` tags. Broker dependency releases use
`minmqtt-vX.Y.Z` tags in the `mqtt_min_broker` repository and are referenced by
`deps.json`.

Current product release tag in this branch:

```text
bridge-v0.1.0
```

## Dependency Rule

Update `deps.json` only after `mqtt_min_broker` has a new released tag.
Temporary fixes under `deps/mqtt_min_broker` should be upstreamed to the broker
repo and replaced by a tag bump.

## Product TODO

The execution TODO list is in [`docs/todo.md`](docs/todo.md).

Version and tag naming rules are in [`docs/versioning.md`](docs/versioning.md).
