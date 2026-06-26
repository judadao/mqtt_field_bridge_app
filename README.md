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

The benchmark records three separate claims. Detailed logs are in
`docs/load_balance_throughput_results.md`.

### 1. Single Broker Speed

Test condition:
- One broker is active; all 8 MQTT clients connect to broker A.
- Workload is 4 publishers, 4 subscribers, and 1 topic for 20 seconds.
- This checks raw broker speed, not fallback.

| Case | Client layout A/B/C/D | Topic count | Msg/s | Delivery |
|------|----------------------:|------------:|------:|---------:|
| mosquitto | `8/0/0/0` | `1` | `28,618.2` | `100.0%` |
| field no-fallback | `8/0/0/0` | `1` | `28,599.0` | `100.0%` |

Result: the field broker is effectively equal to mosquitto for this single-node
workload.

### 2. Recovery Balance

Test condition:
- Four brokers: A, B, C, D.
- Brokers A/B are randomly dropped before client admission.
- Topic count is 16 and field broker client admission limit is 8 clients per
  broker.
- After the drop, each live broker gets 1 publisher and 4 subscribers, so all
  implementations run the same connected client count.
- Topics are local to each live broker, so the result checks recovery health
  without giving field mesh fanout an extra delivery multiplier.
- Mosquitto is the independent-broker baseline with no mesh or fallback.

Column meanings:
- `Req clients`: requested client layout after broker drop.
- `Clients`: connected client layout after admission.
- `Delivery`: delivered messages versus the requested subscriber workload.

| Case | Dropped | Req clients | Clients | Rej subs | Rej pubs | Msg/s | Delivery |
|------|--------:|------------:|--------:|---------:|---------:|------:|---------:|
| mosquitto | `A/B` | `0/0/5/5` | `0/0/5/5` | `0` | `0` | `3,580.85` | `100.0%` |
| field no-fallback | `A/B` | `0/0/5/5` | `0/0/5/5` | `0` | `0` | `3,580.7` | `100.0%` |
| field fallback | `A/B` | `0/0/5/5` | `0/0/5/5` | `0` | `0` | `3,582.7` | `100.0%` |

Result: with the same connected client count and local topics, mosquitto,
field no-fallback, and field fallback recover to the same delivery behavior
after dropping A/B. Fallback capacity gains are measured separately by the
client-limit and topic-limit burst tests, where the requested load exceeds one
broker's remaining capacity.

### 3. Client Limit Balance

Test condition:
- Four field brokers: A, B, C, D.
- Client admission limit is 8 clients per broker.
- Topic count is 1, so topic capacity is not the limit.
- Preload: A has 7 subscribers and 1 publisher, so A is full at 8 clients.
  B/C/D each have 2 subscribers.
- Burst: 18 new subscribers all try broker A first.

Column meanings:
- `Clients A/B/C/D`: total connected clients on each broker after the burst.
- `Rejected burst subs`: burst subscribers that could not connect anywhere.
- `Received`: delivered messages during the 20-second run.

| Case | Clients A/B/C/D | Rejected burst subs | Received | Msg/s |
|------|----------------:|--------------------:|---------:|------:|
| field no-fallback | `8/2/2/2` | `18` | `163,861` | `8,193.05` |
| field fallback | `8/8/8/8` | `0` | `536,816` | `26,840.8` |

Result: fallback uses the spare client capacity on B/C/D. The same burst that is
rejected without fallback is accepted and served with fallback.

### 4. Topic Subscription Limit Balance

Test condition:
- Four field brokers: A, B, C, D.
- Client admission limit is 64 clients per broker, so client count is not the
  limit.
- Topic subscription table limit is 16 entries per broker.
- Test topic count is 64.
- Preload: A has 16 topic subscriptions, so A's topic table is full. B/C/D each
  have 4 topic subscriptions.
- Burst: 36 new topic subscribers all try broker A first.

Column meanings:
- `Topic subs`: accepted subscriber-topic registrations across all brokers.
- `Topics A/B/C/D`: accepted topic subscriptions on each broker.
- `Rejected burst subs`: burst subscribers rejected because no topic slot was
  available on the attempted broker path.

| Case | Topic subs | Topics A/B/C/D | Rejected burst subs | Received | Msg/s | Delivery |
|------|-----------:|----------------:|--------------------:|---------:|------:|---------:|
| field no-fallback | `28` | `16/4/4/4` | `36` | `10,976` | `548.8` | `70.0%` |
| field fallback | `64` | `16/16/16/16` | `0` | `35,805` | `1,790.25` | `100.0%` |

Result: fallback uses the spare topic-table capacity on B/C/D. Topic
subscriptions rise from 28 to 64, and the rejected burst drops from 36 to 0.

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
