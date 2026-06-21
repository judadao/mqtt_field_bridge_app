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
- [x] Merge validated product feature branches back into the mainline branch.

## Dependency Sync

> **mqtt-v0.1.3 is the current pinned broker tag**.
> `deps.json` is already set. Run `scripts/sync_deps.sh` to check out the pinned version.

- [x] Replace the simple `sed` parser in `scripts/sync_deps.sh` with a more
      robust JSON parser if `jq` is available in the build environment.
- [x] Validate that the pinned broker tag exists before checkout.
- [x] Fail the build if `deps/mqtt_min_broker` is dirty.
- [x] Add a command to print the currently pinned broker version.
- [x] Add a local check that `deps.json` and `deps/mqtt_min_broker` match.

## Product App Bootstrap

- [x] Confirm `west build` can include `deps/mqtt_min_broker` through
      `ZEPHYR_EXTRA_MODULES`.
- [x] Use the default `esp32_devkitc/esp32/procpu` board target until a custom
      product overlay is required.
- [x] Decide whether USB-localhost provisioning, AP-mode provisioning, or LAN IP
      provisioning is the first supported setup path.
- [x] Implement product network startup before broker startup.
- [x] Start broker only after network is ready.
- [x] Start P2P only after broker init succeeds.
- [x] Add clear boot logs for network, broker, and P2P state.

## Product Config

- [x] Define persistent config schema for device role/name, WiFi, bridge peers,
      site ID, and topic prefix.
- [x] Store bridge peer config in flash/NVS or Zephyr settings.
- [x] Add defaults for common small and larger peer-count deployments.
- [x] Add config validation.
- [x] Add config reset path for field recovery.

## Bridge Peer Control

- [x] Define peer fields: peer name, host/IP, MQTT port, P2P port, enabled flag.
- [x] Add API to list peers.
- [x] Add API to add/update peers.
- [x] Add API to disable/delete peers.
- [x] Apply peer changes without reboot when possible.
- [x] Add static seed support in `mqtt_min_broker` if current P2P discovery is
      not enough for the field flow.
- [x] Show peer connection state: unknown, connecting, connected, disconnected,
      and last error.
- [x] Add Linux mock simulation for selecting bridge WiFi nodes by peer index.
- [x] Add `/wifi/scan` endpoint with Linux mock backend.
- [x] Build the ESP32 Zephyr WiFi path with the product app.
- [x] Add persistent bridge WiFi state: current SSID, recent SSID list,
      last connection status, and last error/time.
- [x] Add UI flow for `Scan Bridge WiFi` and `Add as Peer Index N`.
- [x] Add `Join Bridge WiFi` action that saves WiFi credentials and adds the
      selected node as a peer.
- [x] Make the current Bridge WiFi broker peer auto-managed in the UI, with
      host/ports reflecting the active Bridge WiFi target instead of manual
      editing.
- [x] Remove manual peer action controls from the provisioning UI; show only
      the active Bridge WiFi broker peer name and connection data.
- [x] Move the mesh/bridge feature checkbox into Bridge Peers as `Auto Bridge`;
      remove the separate Bridge Feature checkbox from the UI.
- [x] Add Bridge WiFi disconnect action and keep Recent entries from showing
      `Current` when the Bridge WiFi state is disconnected.
- [x] Show Bridge WiFi IP roles separately: local STA IP, selected AP/gateway
      IP, and peer broker IP.
- [x] Rename provisioning `Device IP` UI wording to AP/provisioning/broker IP
      so it is not confused with STA WiFi IP.
- [x] Enforce one active bridge WiFi connection at a time; joining a different
      bridge AP must switch the current bridge WiFi before applying peers.
- [x] Add reconnect from recent bridge WiFi list.
- [x] Add delete/remove action for recent Bridge WiFi entries.
- [x] Keep ESP32 join state split into local STA IP, AP/gateway IP, and peer
      broker IP fields so the runtime can populate hardware values.
- [x] Add browser coverage for Scan WiFi, Current Bridge WiFi, Recent Bridge
      WiFi, Join/Switch, and failed join event-log behavior.

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
- [x] Refresh local HTML UX with lightweight CSS-only color, spacing, focus,
      status, and mobile layout improvements.
- [x] Collapse provisioning UI sections, keep controls hidden until login, and
      place the important network/status summary first.
- [x] Move provisioning UI to feature tabs, remove Topic Test/Raw Status from
      the page, hide topic prefix from broker settings, and add peer rows only
      through Add Peer.
- [x] Add DNS to network settings and Linux web/config coverage.
- [x] Add ignored local static-IP shell test for provisioning web config.
- [x] Add browser UI test for peer-index save/delete and event-log failures.
- [x] Keep UI product-specific; do not move it into `mqtt_min_broker`.

## Topic Workflow

- [x] Standardize topic prefix: `site/<site_id>/<stream>`.
- [x] Add product config for `site_id`.
- [x] Add status stream topic.
- [x] Add IO stream topic.
- [x] Add event stream topic.
- [x] Add test publisher flow for a source node.
- [x] Add test subscriber flow for peer nodes.
- [x] Verify peer nodes receive source-node field data through P2P routing.

## Field Validation

- [x] Create manual test checklist for 3-node and 10-node deployments.
- [x] Create scripted test where possible.
- [x] Add scripted 10-node chain validation.
- [x] Add scripted 10-node ring validation.
- [x] Validate peer restart recovery.
- [x] Validate source node continues locally when peers are offline.
- [x] Record logs and expected status page output for each test.

## Hardware Blocked

These items require a target ESP32 device, selected flash partition requirements,
and live WiFi/AP behavior before they can be closed.

- Validate peer reconnection after WiFi reconnect on target ESP32 hardware.
- Replace the current ESP32 WiFi scan/build placeholder with live scan results
  once the board-specific WiFi management path is selected.
- Verify DHCP-derived local STA IP, AP/gateway IP, and peer broker IP values on
  hardware.

## Broker Dependency

Broker-specific issues are tracked in the `mqtt_min_broker` repository, not here.
When this product app needs a broker feature or fix, open a TODO or issue against
`mqtt_min_broker`, implement and test it there, tag a new broker release, then bump
`deps.json` in this repo to pick up the change.
Do not patch `deps/mqtt_min_broker` permanently from this repo.
