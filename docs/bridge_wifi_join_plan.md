# Bridge WiFi Join Plan

## Target Flow

Support field nodes where each ESP32 can expose a setup/bridge AP and users can
choose which nearby bridge node to join.

Example:

```text
MQTT-BRIDGE-node1
MQTT-BRIDGE-node2
MQTT-BRIDGE-node3
```

The product UI should let a user select `node1` or `node2`, then add the
selected node as a peer by peer index.

The Bridge WiFi connection is a single active STA connection. A node can scan
many bridge APs and remember recent APs, but only one bridge WiFi can be
current at a time.

## IP Model

When a browser or another ESP32 connects to a node's AP, that node's AP IP is
also the broker IP for clients on that AP network.

```text
browser -> MQTT-BRIDGE-node1 AP
node1 AP / provisioning / broker IP = 192.168.4.1
browser opens http://192.168.4.1/
```

When one ESP32 bridges to another ESP32 AP, the selected peer broker IP should
come from the connected AP/gateway IP after DHCP. The local STA IP is this node's
client-side address on the selected AP network and is not the same as the AP /
provisioning IP.

```text
node2 STA -> MQTT-BRIDGE-node1 AP
node2 local STA IP = 192.168.4.23
node2 gateway / selected AP IP = 192.168.4.1
node2 peer broker IP = 192.168.4.1
```

## UI Shape

```text
Bridge WiFi
Current Bridge WiFi: MQTT-BRIDGE-node2
Status: connected / disconnected
Local STA IP: 192.168.4.23
Gateway / AP IP: 192.168.4.1
Peer Broker IP: 192.168.4.1
Last event: 2026-06-20 13:20:01 joined MQTT-BRIDGE-node2

[Scan WiFi]

Available Bridge WiFi:
- MQTT-BRIDGE-node1  RSSI -51  [Join / Switch]
- MQTT-BRIDGE-node2  RSSI -63  Current
- MQTT-BRIDGE-node3  RSSI -70  [Join / Switch]
...scroll for more scan results

Recent Bridge WiFi:
- MQTT-BRIDGE-node2  [Reconnect]
- MQTT-BRIDGE-node1  [Reconnect]
```

Joining or switching a Bridge WiFi should:

1. Save the selected SSID/password into the recent list.
2. Set it as the current Bridge WiFi target.
3. Reconnect the ESP32 STA to that AP.
4. Read the local STA IP and gateway/AP IP from the connected netif.
5. Add or update the selected peer index with peer broker IP set to the
   gateway/AP IP.
6. Keep the scan/discovery MQTT and P2P ports for the selected peer.
7. Mark that peer as UI auto-managed while it matches the current Bridge WiFi.
8. Apply peers after the network path is ready.

## Current Code Coverage

- Persistent peer table with `POST /peers/<index>`.
- Bridge peer apply through `bridge_control_apply_peers()`.
- Global `bridge_enabled` gate.
- Provisioning UI with explicit `Peer Index N`, per-peer Save/Delete, Save All,
  and event log timestamps.
- Provisioning web assets split into `app/web/index.html`, `styles.css`, and
  `app.js`, then generated into `app/src/generated/provisioning_index.h`.
- Bridge WiFi UI with current WiFi, scan results, recent WiFi, Join/Switch, and
  Reconnect controls.
- Bridge WiFi current view separates local STA IP, selected AP/gateway IP, and
  peer broker IP.
- The peer matching the current Bridge WiFi broker is shown as auto-managed in
  the UI; users switch it through Bridge WiFi Join/Reconnect instead of editing
  the peer host and ports manually.
- Manual peer action controls are hidden from the web UI for this flow. The UI
  only shows the active Bridge WiFi broker peer name, peer index, broker IP,
  MQTT/P2P ports, local STA IP, and gateway/AP IP.
- The Broker tab does not expose a Mesh checkbox. The same persisted mesh/bridge
  control is presented in Bridge Peers as `Auto Bridge`.
- Connected scan/recent rows show `Current` and a `Disconnect` action. Rows that
  are not current show `Connect`; disconnected recent rows must not show
  `Current`.
- Bridge WiFi endpoints:
  - `GET /wifi/scan`
  - `GET /bridge-wifi/current`
  - `GET /bridge-wifi/recent`
  - `POST /bridge-wifi/join`
- Persistent bridge WiFi state for current target, recent entries, enable flag,
  connection status, local STA IP, gateway IP, last event, and last error.
- Linux browser test for peer-index save/delete request routing and bridge WiFi
  scan/join/error-log behavior.
- Linux mock shell simulation for bridge WiFi selection:
  `make -C tests/linux bridge-wifi-sim-test`.

## Linux Simulation Scope

The simulation does not emulate real ESP32 SoftAP/STA radio behavior. It starts
three product web control planes on localhost and uses 10 mock scan entries.
The first three are:

```text
MQTT-BRIDGE-node1 -> host 127.0.0.2 mqtt 11883 p2p 14884
MQTT-BRIDGE-node2 -> host 127.0.0.3 mqtt 11884 p2p 14886
MQTT-BRIDGE-node3 -> host 127.0.0.4 mqtt 11885 p2p 14888
```

The Linux control-plane HTTP servers still listen on `127.0.0.1` with distinct
ports. The mock broker/AP addresses use different loopback IPs so the UI and
tests can verify that peer broker IP is not assumed to be the web server IP.
The UI keeps the scan list at a fixed height of about three rows and scrolls
inside the list for the remaining mock scan results.

It verifies:

- node2 can join node1 at peer index 0.
- current Bridge WiFi reports local STA IP, gateway/AP IP, and peer broker IP.
- node3 can add node1 and node2 as separate selectable bridge peers.
- deleting peer index 1 does not clear peer index 0.

## Remaining Implementation Tasks

- On Zephyr, back `GET /wifi/scan` with ESP32 WiFi scan results.
- Wire `POST /bridge-wifi/join` to the real ESP32 STA reconnect path. Linux
  currently records the selected target and applies the peer table.
- On ESP32, derive peer broker IP from the connected AP/gateway IP after DHCP
  and expose the assigned local STA IP in `GET /bridge-wifi/current`.
- Add `DELETE /bridge-wifi/recent/<index>` and a matching UI remove action if
  recent entries need manual cleanup.
- Validate the AP+STA channel/subnet constraints on target ESP32 hardware.

## Constraints

- ESP32 STA can connect to one AP at a time.
- AP+STA shares one radio and usually one WiFi channel.
- All nodes should avoid identical AP subnets if the field topology requires IP
  routing beyond broker-to-broker P2P.
- The first production flow should support common-WiFi and single-upstream/hub
  topologies before attempting arbitrary AP+STA chains.
