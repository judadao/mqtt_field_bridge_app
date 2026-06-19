# Linux Test Suite

All tests run on Linux without Zephyr hardware. Dual-version: the same product
logic is compiled with a Zephyr shim header so it runs on the host.

## Quick start

```bash
# Unit tests only (no broker needed)
make -C tests/linux unit-tests

# Integration tests (builds broker with P2P automatically)
make -C tests/linux integration-tests

# Stress tests
make -C tests/linux stress

# Everything
make -C tests/linux test
```

## Test inventory

| Test | Type | What it covers |
|------|------|----------------|
| `unit_product_config` | unit | peer CRUD, boundary, null-safety, Linux persistence |
| `unit_bridge_control` | unit | enabled peer filtering and invalid peer skipping |
| `unit_provisioning_http` | unit | socket-based `/status`, `/peers`, `POST /peers/<index>`, and error routes |
| `test_sync_deps.sh` | shell | `--version`, dirty check, missing tag, idempotency |
| `test_3node_scenario.sh` | integration | Note1→Note2 routing, Note3 routing, Note1 local-only when Note2 offline, Note2 restart recovery |
| `stress_reconnect.sh` | stress | 5 kill+restart cycles by default; B1 must not hang |
| `stress_throughput.sh` | stress | 3-broker P2P under multi-publisher load; minimum throughput check |

## Knobs

| Variable | Default | Applies to |
|----------|---------|-----------|
| `SETTLE_SEC` | 5 | 3node, reconnect, throughput |
| `VERIFY_TIMEOUT_SEC` | 5 | reconnect |
| `RESTART_COUNT` | 5 | reconnect |
| `PUB_COUNT` | 5 | throughput |
| `SUB_COUNT` | 3 | throughput |
| `DURATION` | 10 | throughput |
| `MIN_THROUGHPUT_MSG` | 500 | throughput |

## Zephyr shim

`include/zephyr/logging/log.h` stubs out Zephyr logging macros.
Set `MQTT_TEST_VERBOSE=1` in `CFLAGS` to enable log output during unit tests.
