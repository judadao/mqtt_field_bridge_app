# MQTT Bridge Product TODO

This TODO is for the product application repository. The broker dependency is
`deps/mqtt_min_broker` and must be pinned by `deps.json`.

## Repo And Versioning

- [ ] Keep this repo as the product app repo, separate from `mqtt_min_broker`.
- [x] Use `deps.json` to pin the broker module tag.
- [ ] Use product tags with the `bridge-vX.Y.Z` format.
- [ ] Use broker dependency tags with the `minmqtt-vX.Y.Z` format when the
      broker repo is ready to adopt the prefixed scheme.
- [ ] Do not patch `deps/mqtt_min_broker` permanently from this repo.
- [ ] When broker fixes are needed, create an issue/TODO against
      `mqtt_min_broker`, fix there, tag there, then bump `deps.json` here.

## Dependency Sync

> **minmqtt-v0.1.0 is the current pinned broker tag** (tagged 2026-06-19).
> `deps.json` is already set. Run `scripts/sync_deps.sh` to check out the pinned version.

- [ ] Replace the simple `sed` parser in `scripts/sync_deps.sh` with a more
      robust JSON parser if `jq` is available in the build environment.
- [ ] Validate that the pinned broker tag exists before checkout.
- [ ] Fail the build if `deps/mqtt_min_broker` is dirty.
- [ ] Add a command to print the currently pinned broker version.
- [ ] Add CI or a local check that `deps.json` and `deps/mqtt_min_broker` match.

## Product App Bootstrap

- [ ] Confirm `west build` can include `deps/mqtt_min_broker` through
      `ZEPHYR_EXTRA_MODULES`.
- [ ] Add board-specific overlays for the target ESP32 board.
- [ ] Decide whether USB-localhost provisioning, AP-mode provisioning, or LAN IP
      provisioning is the first supported setup path.
- [ ] Implement product network startup before broker startup.
- [ ] Start broker only after network is ready.
- [ ] Start P2P only after broker init succeeds.
- [ ] Add clear boot logs for network, broker, and P2P state.

## Product Config

- [ ] Define persistent config schema for device role/name, WiFi, bridge peers,
      site ID, and 4510 topic prefix.
- [ ] Store config in flash/NVS or Zephyr settings.
- [ ] Add defaults for Note 1, Note 2, and Note 3.
- [ ] Add config validation.
- [ ] Add config reset path for field recovery.

## Bridge Peer Control

- [ ] Define peer fields: peer name, host/IP, MQTT port, P2P port, enabled flag.
- [ ] Add API to list peers.
- [ ] Add API to add/update peers.
- [ ] Add API to disable/delete peers.
- [ ] Apply peer changes without reboot when possible.
- [ ] Add static seed support in `mqtt_min_broker` if current P2P discovery is
      not enough for the field flow.
- [ ] Show peer connection state: unknown, connecting, connected, disconnected,
      and last error.

## Local HTML / Provisioning

- [ ] Implement HTTP server for the product app.
- [ ] Add status endpoint with WiFi state, IP address, broker state, P2P role,
      connected peers, and remote subscription count.
- [ ] Add WiFi setup endpoint.
- [ ] Add broker start/stop/status endpoints.
- [ ] Add peer configuration endpoints.
- [ ] Add publish test endpoint.
- [ ] Add simple HTML UI for status, WiFi setup, peer setup, broker/P2P state,
      and 4510 topic test.
- [ ] Keep UI product-specific; do not move it into `mqtt_min_broker`.

## 4510 Workflow

- [ ] Standardize topic prefix: `site/<site_id>/4510/<stream>`.
- [ ] Add product config for `site_id`.
- [ ] Add status stream topic: `site/<site_id>/4510/status`.
- [ ] Add IO stream topic: `site/<site_id>/4510/io`.
- [ ] Add event stream topic: `site/<site_id>/4510/event`.
- [ ] Add test publisher flow for Note 1.
- [ ] Add test subscriber flow for Note 2 and Note 3.
- [ ] Verify Note 2 and Note 3 receive Note 1 4510 data through P2P routing.

## Field Validation

- [ ] Create 3-node manual test checklist.
- [ ] Create scripted test where possible.
- [ ] Validate Note 2 restart recovery.
- [ ] Validate Note 3 restart recovery.
- [ ] Validate Note 1 continues locally when Note 2 or Note 3 is offline.
- [ ] Validate peer reconnection after WiFi reconnect.
- [ ] Record logs and expected status page output for each test.

## Broker Dependency

Broker-specific issues are tracked in the `mqtt_min_broker` repository, not here.
When this product app needs a broker feature or fix, open a TODO or issue against
`mqtt_min_broker`, implement and test it there, tag a new broker release, then bump
`deps.json` in this repo to pick up the change.
Do not patch `deps/mqtt_min_broker` permanently from this repo.
