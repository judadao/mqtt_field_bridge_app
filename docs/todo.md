# MQTT Bridge Product TODO

This TODO is for the product application repository. The broker dependency is
`deps/mqtt_min_broker` and must be pinned by `deps.json`.

## Repo And Versioning

- [x] Keep this repo as the product app repo, separate from `mqtt_min_broker`.
- [x] Use `deps.json` to pin the broker module tag.
- [x] Use product tags with the `bridge-vX.Y.Z` format.
- [x] Use broker dependency tags with the `minmqtt-vX.Y.Z` format when the
      broker repo is ready to adopt the prefixed scheme.
- [x] Do not patch `deps/mqtt_min_broker` permanently from this repo.
- [x] When broker fixes are needed, create an issue/TODO against
      `mqtt_min_broker`, fix there, tag there, then bump `deps.json` here.

## Dependency Sync

> **minmqtt-v0.1.12 is the current pinned broker tag** (tagged 2026-06-19).
> `deps.json` is already set. Run `scripts/sync_deps.sh` to check out the pinned version.

- [x] Replace the simple `sed` parser in `scripts/sync_deps.sh` with a more
      robust JSON parser if `jq` is available in the build environment.
- [x] Validate that the pinned broker tag exists before checkout.
- [x] Fail the build if `deps/mqtt_min_broker` is dirty.
- [x] Add a command to print the currently pinned broker version.
- [x] Add a local check that `deps.json` and `deps/mqtt_min_broker` match.

## Product App Bootstrap

- [ ] Confirm `west build` can include `deps/mqtt_min_broker` through
      `ZEPHYR_EXTRA_MODULES`.
- [ ] Add board-specific overlays for the target ESP32 board.
- [x] Decide whether USB-localhost provisioning, AP-mode provisioning, or LAN IP
      provisioning is the first supported setup path.
- [ ] Implement product network startup before broker startup.
- [ ] Start broker only after network is ready.
- [ ] Start P2P only after broker init succeeds.
- [ ] Add clear boot logs for network, broker, and P2P state.

## Product Config

- [x] Define persistent config schema for device role/name, WiFi, bridge peers,
      site ID, and topic prefix.
- [x] Store bridge peer config in flash/NVS or Zephyr settings.
- [ ] Add defaults for common small and larger peer-count deployments.
- [x] Add config validation.
- [x] Add config reset path for field recovery.

## Bridge Peer Control

- [x] Define peer fields: peer name, host/IP, MQTT port, P2P port, enabled flag.
- [x] Add API to list peers.
- [x] Add API to add/update peers.
- [x] Add API to disable/delete peers.
- [ ] Apply peer changes without reboot when possible.
- [ ] Add static seed support in `mqtt_min_broker` if current P2P discovery is
      not enough for the field flow.
- [ ] Show peer connection state: unknown, connecting, connected, disconnected,
      and last error.

## Local HTML / Provisioning

- [x] Implement HTTP server for the product app.
- [x] Add status endpoint with WiFi state, IP address, broker state, P2P role,
      connected peers, and remote subscription count.
- [x] Add WiFi setup endpoint.
- [x] Add broker start/stop/status endpoints.
- [x] Add peer configuration endpoints.
- [x] Add publish test endpoint.
- [x] Add simple HTML UI for status and peer setup.
- [x] Add HTML UI for WiFi setup, broker/P2P state, and topic test.
- [x] Keep UI product-specific; do not move it into `mqtt_min_broker`.

## Topic Workflow

- [x] Standardize topic prefix: `site/<site_id>/<stream>`.
- [x] Add product config for `site_id`.
- [ ] Add status stream topic.
- [ ] Add IO stream topic.
- [ ] Add event stream topic.
- [ ] Add test publisher flow for a source node.
- [ ] Add test subscriber flow for peer nodes.
- [ ] Verify peer nodes receive source-node field data through P2P routing.

## Field Validation

- [x] Create manual test checklist for 3-node and 10-node deployments.
- [x] Create scripted test where possible.
- [x] Add scripted 10-node chain validation.
- [x] Add scripted 10-node ring validation.
- [x] Validate peer restart recovery.
- [x] Validate source node continues locally when peers are offline.
- [ ] Validate peer reconnection after WiFi reconnect.
- [x] Record logs and expected status page output for each test.

## Broker Dependency

Broker-specific issues are tracked in the `mqtt_min_broker` repository, not here.
When this product app needs a broker feature or fix, open a TODO or issue against
`mqtt_min_broker`, implement and test it there, tag a new broker release, then bump
`deps.json` in this repo to pick up the change.
Do not patch `deps/mqtt_min_broker` permanently from this repo.
