# Bridge Memory

Persistent notes for ongoing bridge/product iteration.

## 2026-06-19 Broker Dependency Bump

New broker release available and adopted:

- Broker repo: `mqtt_min_broker`
- New tag: `minmqtt-v0.1.12`
- Bridge action: `deps.json` now pins `minmqtt-v0.1.12`
- Bridge release target: `bridge-v0.1.1`
- Sync command: `./scripts/sync_deps.sh`

Why this matters for the bridge app:

- The embedded broker dependency now includes parser-level malformed packet
  hardening for CONNECT/PUBLISH/SUBSCRIBE/UNSUBSCRIBE helpers.
- SUBSCRIBE and UNSUBSCRIBE topic-list validation is stricter at parser level,
  so bridge/product tests should rely on malformed topic-list packets being
  rejected before handler logic.
- Continue product optimization on top of released broker tags; do not patch
  `deps/mqtt_min_broker` directly from this repo.

Next bridge optimization direction:

- Keep dependency sync tests version-agnostic so future broker tag bumps only
  require `deps.json` and docs/memory updates.
- Use `./scripts/sync_deps.sh --check-latest` to tell bridge maintainers when a
  newer `minmqtt-v*` broker release is available.
- Continue improving product-owned WiFi/provisioning/broker-control flows in
  this repo while keeping reusable broker behavior upstreamed in
  `mqtt_min_broker`.

## 2026-06-19 Provisioning Runtime Completion

Completed product-owned provisioning/runtime surface:

- Persistent validated settings now cover system, network, broker/site/topic,
  and bridge peer slots.
- `/login` issues a short-lived in-memory token; authenticated routes require
  `X-Auth-Token`.
- `/config`, `/config/reset`, `/peers`, `/broker/control`, and `/publish-test`
  are implemented and covered by Linux HTTP tests.
- Firmware-served HTML now covers Login, System Setting, Network Setting,
  Broker Setting / Bridge Mesh Setting, runtime status cards, broker control,
  reset, and Topic Test.
- Unit coverage now includes `unit_product_runtime` and `unit_product_topics`;
  last local unit run passed `unit_product_config` 227/227,
  `unit_product_runtime` 54/54, `unit_product_topics` 20/20,
  `unit_bridge_control` 12/12, and `unit_provisioning_http` 159/159.
- Enabled IPv4 bridge peers are applied to the broker static seed table through
  `p2p_static_seed_clear()` / `p2p_static_seed_add()`.
- `/peer-status` exposes disabled, connecting, connected, disconnected, and
  unknown peer states with last-error text.
- Provisioning HTML received a lightweight CSS-only UX refresh with restrained
  teal/amber status colors, clearer segmented navigation, stronger form focus
  states, responsive mobile layout, and no external image/font/icon dependency.
- Network settings now include DNS in persistent config, `/config` JSON, and
  the provisioning UI. `tests/linux/test_web_network_config.sh` reads ignored
  `tests/linux/local_web_network.env` for site-specific static-IP web tests.

Remaining meaningful blockers:

- Saved WiFi/network settings still need target ESP32 driver application.
- Live broker stop/restart after boot requires lifecycle support in
  `mqtt_min_broker`; current broker API exposes a blocking `broker_run()`.
