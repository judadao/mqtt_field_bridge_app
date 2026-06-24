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

# Everything
make -C tests/linux test
```

## Test inventory

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
| `test_esp32_network_bind.sh` | hardware/network | Validates ESP32 management IP, broker IP, ARP/ping, HTTP status, and MQTT bind isolation |
| `test_chain_scale.sh` | scale | 10-node chain or ring; first-node publish reaches last-node subscriber |
| `stress_reconnect.sh` | stress | 5 kill+restart cycles by default; B1 must not hang |
| `stress_throughput.sh` | stress | 3-broker P2P under multi-publisher load; minimum throughput check |

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
| `WEB_TEST_DEVICE_IP` | required by `web-network-test` | web network config |
| `WEB_TEST_GATEWAY` | required by `web-network-test` | web network config |
| `WEB_TEST_NETMASK` | required by `web-network-test` | web network config |
| `WEB_TEST_DNS` | required by `web-network-test` | web network config |
| `ESP32_HOST` | `192.168.127.4` | ESP32/Linux chain bridge hardware test |
| `ESP32_DEVICE_IP` | `192.168.127.4` | ESP32 management IP for network bind test |
| `ESP32_BROKER_IP` | `192.168.127.15` | ESP32 broker IP for network bind test |
| `IFACE` | route-derived | Host network interface for ARP/tcpdump validation |
| `LINUX_PEER_HOST` | `192.168.127.5` | IP address the ESP32 uses to reach Linux broker5 |

`web-network-test` sources `tests/linux/local_web_network.env` when present.
That file is ignored so site-specific IP settings stay local.

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
