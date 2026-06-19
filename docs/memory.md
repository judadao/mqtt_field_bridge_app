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
  `unit_product_runtime` 34/34, `unit_product_topics` 20/20,
  `unit_bridge_control` 7/7, and `unit_provisioning_http` 150/150.

Remaining meaningful blockers:

- Saved WiFi/network settings still need target ESP32 driver application.
- Live broker stop/restart after boot requires lifecycle support in
  `mqtt_min_broker`; current broker API exposes a blocking `broker_run()`.
