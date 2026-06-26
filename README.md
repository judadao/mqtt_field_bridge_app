# mqtt_field_bridge_app

Product application for configurable MQTT field bridge deployments.

## Quick Start: Linux Web UI

```sh
git clone git@github.com:judadao/mqtt_field_bridge_app.git
cd mqtt_field_bridge_app
./run_linux_web.sh
```

Open `http://127.0.0.1:8080/`. The Ethernet provisioning UI loads directly;
there is no login step.

This starts the product provisioning UI on Linux. It is the fastest way to see
and test the web flow before using ESP32 hardware.

## Overview

`mqtt_field_bridge_app` composes Dephy modules into a deployable ESP32 field
bridge. It owns provisioning UI/HTTP APIs, product configuration, Ethernet
network setup, manual broker peer selection, runtime status, and product-level
Linux tests.

## Key Value

- Product integration of `mqtt_min_broker`, `dephy`, `dephy_industrial_io`, and
  `dephy_testkit`.
- Embedded provisioning UI for Ethernet network settings, MQTT/P2P broker
  controls, and manual broker peer config.
- Static-seed P2P product path using `CONFIG_MQTT_P2P_DYNAMIC=y` and
  `CONFIG_MQTT_P2P_STATIC_SEEDS_ONLY=y`.
- Linux web runner and tests for config, provisioning rendering, peer application, runtime
  status, sync deps, and reconnect stress.
- Keeps reusable broker/IO/board behavior in module repos instead of product
  source.

## Linux Commands

```sh
# Fast local validation
make -C tests/linux unit-tests

# Full Linux product tests, including P2P scenarios and stress tests
make -C tests/linux test
```

`run_linux_web.sh` uses sibling module checkouts when they exist. Otherwise it
downloads the pinned dependency versions into `deps/`.

## Load-Balance Throughput Results

The benchmark records two product claims: a single field broker is near
mosquitto speed, and admission fallback improves total client/topic capacity
when one broker is hot and peer brokers still have room.

Detailed logs are in `docs/load_balance_throughput_results.md`.

### Single Broker Speed

This isolates broker speed with the same eight clients concentrated on broker A.

| Case | Client layout | Topic count | Msg/s | Delivery |
|------|---------------|------------:|------:|---------:|
| mosquitto | `8/0/0/0` | `1` | `28,618.2` | `100.0%` |
| field no-fallback | `8/0/0/0` | `1` | `28,599.0` | `100.0%` |

### Dynamic Balance Advantage

The dynamic-balance test starts with broker A already full and B/C/D lightly
loaded. Then 18 new subscribers all try broker A first.

Column meanings:

- `Clients A/B/C/D`: total connected MQTT clients on each broker.
- `Rejected burst subs`: new burst subscribers that could not connect.
- `Topic subscriptions`: accepted subscriber-topic registrations.
- `Topics A/B/C/D`: distinct subscribed topics present on each broker.

#### Client Balancing

This run uses one topic and measures whether overflow clients are accepted.

| Case | Clients A/B/C/D | Rejected burst subs | Received | Msg/s |
|------|----------------:|--------------------:|---------:|------:|
| field no-fallback | `8/2/2/2` | `18` | `163,861` | `8,193.05` |
| field fallback | `8/8/8/8` | `0` | `536,816` | `26,840.8` |

Fallback uses the spare capacity on B/C/D, so the same burst that is rejected
without fallback becomes accepted work.

#### Topic Capacity

This run uses 32 topics and measures topic-subscription capacity separately from
raw message speed.

| Case | Topic subscriptions | Topics A/B/C/D | Rejected burst subs | Delivery |
|------|--------------------:|----------------:|--------------------:|---------:|
| field no-fallback | `13` | `7/2/2/2` | `18` | `100.0%` |
| field fallback | `31` | `7/8/8/8` | `0` | `100.0%` |

Fallback raises accepted topic subscriptions from 13 to 31 and keeps all four
brokers at their admission limit. This is the load-balancing benefit: higher
total served work across the broker network.

## ESP32 / Dephy Build

Use this path when you are ready to build firmware for the Dephy ESP32 target:

```sh
# Build from local sibling module checkouts
./scripts/sync_deps.sh local-build

# Or build from pinned git dependencies
./scripts/sync_deps.sh external-build
```

`local-build` is for multi-repo development under one workspace.
`external-build` is for a clean product build from `deps.json`.

## Architecture Flow

```mermaid
flowchart LR
    UI[Provisioning web UI] --> HTTP[provisioning_http]
    HTTP --> Config[product_config]
    Config --> Runtime[product_runtime]
    Runtime --> Broker[mqtt_min_broker]
    Runtime --> P2P[P2P peer control]
    IO[dephy_industrial_io] --> Broker
    Board[dephy ESP32 profile] --> App[Zephyr product app]
    Testkit[dephy_testkit] --> Tests[Linux product tests]
```

## Example User Scenario

```mermaid
flowchart TD
    A[Installer opens provisioning UI] --> B[Set Ethernet DHCP/static network and MQTT ports]
    B --> C[Choose broker slot and enter broker host/IP]
    C --> D[Save peer as static P2P seed]
    D --> E[ESP32 starts local broker]
    E --> F[Peer brokers connect]
    F --> G[Field IO publishes to site topic]
    G --> H[Remote subscriber receives bridged data]
```

## Simple Principle

This repo owns product workflow and module composition. If a behavior is
reusable, fix it in the module repo first, tag it, then update `deps.json`.

## Systematic Regression Testing

From the workspace root, run the shared pytest regression module:

```sh
../dephy_testkit/.venv/bin/python -m pytest ../dephy_testkit/tests/regression --module mqtt_field_bridge_app
../dephy_testkit/.venv/bin/python -m pytest ../dephy_testkit/tests/regression --module mqtt_field_bridge_app --profile integration
../dephy_testkit/.venv/bin/python -m pytest ../dephy_testkit/tests/regression --module mqtt_field_bridge_app --profile full
```

The local repo tests remain:

```sh
make -C tests/linux unit-tests provisioning-render-size
make -C tests/linux integration-tests
make -C tests/linux test
```

`make -C tests/linux test` is the canonical local entry point and triggers the
main suites through `dephy_testkit` using `tests/linux/trigger_testkit.sh`.
When a test case or test script changes, update the direct Makefile target and
the matching `testkit-*` wrapper so regression and CI runs keep using testkit
result reporting.

## Docs

- `docs/readme_legacy.md`: previous long README and detailed examples.
- `docs/field_bridge_scenario.md`: field bridge scenario notes.
- `docs/field_validation_checklist.md`: hardware validation checklist.
- `docs/bridge_wifi_join_plan.md`: legacy Bridge WiFi join plan.
- `docs/versioning.md`: dependency/version guidance.
- `docs/todo.md`: current TODO summary.

## License

MIT. See `LICENSE` and `NOTICE.md`. Reuse and references are allowed, but the
copyright notice and attribution to Judd (judadao) must be preserved.
