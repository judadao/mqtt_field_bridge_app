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
- Continue improving product-owned WiFi/provisioning/broker-control flows in
  this repo while keeping reusable broker behavior upstreamed in
  `mqtt_min_broker`.
