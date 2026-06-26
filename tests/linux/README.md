# Linux Test Suite

All tests run on Linux without Zephyr hardware. Dual-version: the same product
logic is compiled with a Zephyr shim header so it runs on the host.

## Quick start

```bash
# Unit tests only (no broker needed)
make -C tests/linux unit-tests

# Provisioning web/config test with ignored local static IP settings
make -C tests/linux web-network-test

# Start the Linux provisioning web UI for browser inspection
make -C tests/linux run-web-server

# Integration tests (builds broker with P2P automatically)
make -C tests/linux integration-tests

# Stress tests
make -C tests/linux stress

# Scale tests (10-node chain by default)
make -C tests/linux scale-tests

# Redundant 10-node ring scale test
make -C tests/linux scale-ring-tests

# Everything through dephy_testkit result wrappers
make -C tests/linux test
```

## Test inventory

The default `test` target runs the main suites through
`tests/linux/trigger_testkit.sh`, which delegates to
`dephy_testkit/scripts/run_with_result.sh` and emits JSON result lines. Keep
direct targets such as `unit-tests`, `integration-tests`, and `stress` available
for debugging, but route default and CI-style runs through `testkit-*` targets.
When adding or changing a test case or shell/browser test script, update the
direct Makefile target and the corresponding `testkit-*` wrapper in the same
change.

| Test | Type | What it covers |
|------|------|----------------|
| `unit_product_config` | unit | peer CRUD, boundary, null-safety, Linux persistence |
| `unit_bridge_control` | unit | enabled peer filtering and invalid peer skipping |
| `unit_provisioning_http` | unit | socket-based local HTML page, `/status`, `/peers`, `POST /peers/<index>`, and error routes |
| `test_web_network_config.sh` | shell/unit | provisioning web `/config` save/read using ignored local static IP, gateway, netmask, and DNS values |
| `ui_browser_test.js` | browser | headless Chrome UI flow for direct-load Ethernet UI, manual broker slot editing, peer-index save routing, and removed login/WiFi controls |
| `run_web_config_server.sh` | manual | starts the Linux provisioning web UI at `http://127.0.0.1:8080/` using ignored local static network values |
| `test_sync_deps.sh` | shell | `--version`, dirty check, missing tag, idempotency |
| `test_3node_scenario.sh` | integration | Node1→Node2 routing, Node3 routing, Node1 local-only when Node2 offline, Node2 restart recovery |
| `test_esp32_linux_chain_bridge.sh` | hardware/integration | Linux broker1-5 chain bridged to ESP32 through broker5; verifies publish ESP32→broker1 and broker1→ESP32 |
| `test_esp32_linux_esp32_bridge.sh` | hardware/integration | Two W5500 ESP32 brokers bridged through one Linux middle broker; verifies publish ESP A→ESP B and ESP B→ESP A |
| `test_esp32_linux_admission_fallback.sh` | hardware/integration | Mode A admission fallback: ESP32 rejects overflow local client, client falls back to Linux, and bridge delivery remains intact |
| `test_dual_esp32_admission_fallback.sh` | hardware/integration | Mode A admission fallback from one W5500 ESP32 broker to another W5500 ESP32 broker |
| `test_esp32_network_bind.sh` | hardware/network | Validates ESP32 management IP, broker IP, ARP/ping, HTTP status, and MQTT bind isolation |
| `test_chain_scale.sh` | scale | 10-node chain or ring; first-node publish reaches last-node subscriber |
| `stress_reconnect.sh` | stress | 5 kill+restart cycles by default; B1 must not hang |
| `stress_throughput.sh` | stress | 3-broker P2P under multi-publisher load; minimum throughput check |
| `run_load_balance_matrix.sh` | benchmark | Mosquitto, field no-fallback, and field fallback comparison for hotspot and uneven client load |
| `run_fair4_capacity_compare.sh` | benchmark | 4-broker comparison of concentrated `8/0/0/0` capacity against fallback-distributed `2/2/2/2` capacity |
| `run_dynamic_balance_burst.sh` | benchmark | Hot-broker burst test showing fallback client-capacity gains |
| `run_topic_limit_burst.sh` | benchmark | Hot-broker topic-table test showing fallback topic-subscription capacity gains |

## Knobs

| Variable | Default | Applies to |
|----------|---------|-----------|
| `SETTLE_SEC` | 12 for 3node, 5 for stress | 3node, reconnect, throughput |
| `NODE_COUNT` | 10 | chain scale |
| `TOPOLOGY` | chain | chain scale |
| `WAIT_MSG_SEC` | 12 for 3node, 10 for chain scale | 3node, chain scale |
| `SUB_PROP_SEC` | 4 | 3node |
| `VERIFY_TIMEOUT_SEC` | 5 | reconnect |
| `RESTART_COUNT` | 5 | reconnect |
| `PUB_COUNT` | 5 | throughput |
| `SUB_COUNT` | 3 | throughput |
| `DURATION` | 10 | throughput |
| `MIN_THROUGHPUT_MSG` | 500 | throughput |
| `ADMISSION` | 12 | load-balance matrix |
| `PUBLISH_DELAY` | 0.0005 | load-balance matrix |
| `TOPIC_COUNT` | 1 | dynamic balance burst |
| `TOPIC_MAX_SUBS` | 16 | topic-limit burst |
| `WEB_TEST_DEVICE_IP` | required by `web-network-test` | web network config |
| `WEB_TEST_GATEWAY` | required by `web-network-test` | web network config |
| `WEB_TEST_NETMASK` | required by `web-network-test` | web network config |
| `WEB_TEST_DNS` | required by `web-network-test` | web network config |
| `ESP32_HOST` | `192.168.127.4` | ESP32/Linux chain bridge hardware test |
| `ESP32_DEVICE_IP` | `192.168.127.4` | ESP32 management IP for network bind test |
| `ESP32_BROKER_IP` | `192.168.127.15` | ESP32 broker IP for network bind test |
| `ESP_A_HTTP` / `ESP_A_BROKER` | `192.168.127.4` / `192.168.127.15` | First ESP32 for the ESP-Linux-ESP bridge test |
| `ESP_B_HTTP` / `ESP_B_BROKER` | `192.168.127.6` / `192.168.127.16` | Second ESP32 for the ESP-Linux-ESP bridge test |
| `IFACE` | route-derived | Host network interface for ARP/tcpdump validation |
| `LINUX_PEER_HOST` | `192.168.127.5` | IP address the ESP32 uses to reach Linux broker5 |
| `LINUX_HOST` | `192.168.127.5` | Linux middle broker IP visible from ESP32 bridge peers |
| `BRIDGE_MODE` | `star` | `star` makes both ESP32 boards seed Linux; `chain` makes Linux seed ESP B |

`web-network-test` sources `tests/linux/local_web_network.env` when present.
That file is ignored so site-specific IP settings stay local.

## Load-balance benchmark

Run the comparative benchmark in `tmux` so long runs survive terminal
disconnects:

```bash
tmux new-session -d -s lb-throughput \
  'cd /home/judd/moxa/personal/mqtt_field_bridge_app && tests/linux/run_load_balance_matrix.sh'
```

Results are written under `tests/linux/out/load_balance_matrix/` and appended to
`docs/load_balance_throughput_results.md`. The current benchmark direction is
that fallback raises total accepted clients, while bridge fanout can reduce raw
message rate. A follow-up case should test four brokers with admission `2` each
and a target `2/2/2/2` distribution, to measure fully distributed throughput
separately from hotspot overflow.

The concentrated-versus-distributed capacity comparison is:

```bash
tmux new-session -d -s fair4-capacity \
  'cd /home/judd/moxa/personal/mqtt_field_bridge_app && tests/linux/run_fair4_capacity_compare.sh'
```

The recorded 20260626 run showed mosquitto at `28,618.2 msg/s` with `8/0/0/0`,
field no-fallback at `28,599.0 msg/s` with `8/0/0/0`, and field fallback at
`16,462.6 msg/s` with `2/2/2/2`.

### Recovery balance

The random peer drop recovery run is:

```bash
tmux new-session -d -s random-drop \
  'cd /home/judd/moxa/personal/mqtt_field_bridge_app && tests/linux/run_random_drop_recovery.sh'
```

This randomly terminates brokers, then admits publishers and subscribers that
initially target A/B/C/D. No-fallback clients targeting dropped brokers should
be rejected; fallback clients should land on the remaining live brokers when
capacity is available. Mosquitto is included as the independent-broker baseline
with no mesh or fallback.

The recorded 20260626 run used admission `8`, topic count `16`, drop count `2`,
and seed `260626`, which dropped brokers A/B.

| Impl | Dropped brokers | Req clients A/B/C/D | Conn clients A/B/C/D | Rej subs | Rej pubs | Fallback subs | Fallback pubs | Msg/s | Requested delivery % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mosquitto | A/B | 5/5/5/5 | 0/0/5/5 | 8 | 2 | 0 | 0 | 895.0 | 25.0 |
| field_no_fallback | A/B | 5/5/5/5 | 0/0/5/5 | 8 | 2 | 0 | 0 | 1,791.15 | 50.0 |
| field_fallback | A/B | 5/5/5/5 | 0/0/8/8 | 4 | 0 | 12 | 2 | 5,367.45 | 75.0 |

The client-limit burst run is:

```bash
tmux new-session -d -s dynamic-client \
  'cd /home/judd/moxa/personal/mqtt_field_bridge_app && tests/linux/run_dynamic_balance_burst.sh'
```

The recorded 20260626 run used admission `8`, preloaded clients `8/2/2/2`, then
sent 18 new subscribers to broker A. No-fallback stayed at `8/2/2/2` and
rejected all 18; fallback reached `8/8/8/8` and rejected none.

The topic-limit burst run is:

```bash
tmux new-session -d -s topic-limit \
  'cd /home/judd/moxa/personal/mqtt_field_bridge_app && tests/linux/run_topic_limit_burst.sh'
```

The recorded 20260626 run used client admission `64` and topic table limit `16`,
preloaded topic subscriptions `16/4/4/4`, then sent 36 new topic subscribers to
broker A. No-fallback stayed at `16/4/4/4` and rejected all 36; fallback reached
`16/16/16/16` and rejected none.

## Manual web UI

Create or edit the ignored local file:

```bash
cat > tests/linux/local_web_network.env <<'EOF'
WEB_TEST_DEVICE_IP=10.90.66.226
WEB_TEST_GATEWAY=10.90.66.1
WEB_TEST_NETMASK=255.255.254.0
WEB_TEST_DNS=10.123.200.11
EOF
```

Start the Linux provisioning web UI:

```bash
make -C tests/linux run-web-server
```

Then open `http://127.0.0.1:8080/`. The UI loads without login. Stop the server
with `Ctrl-C`.

## Zephyr shim

`include/zephyr/logging/log.h` stubs out Zephyr logging macros.
Set `MQTT_TEST_VERBOSE=1` in `CFLAGS` to enable log output during unit tests.
