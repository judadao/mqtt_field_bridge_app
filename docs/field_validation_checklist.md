# Field Validation Checklist

Use this checklist for hardware or lab validation after the Linux suite passes.

## Common Setup

- Build and flash each ESP32 target with the same product firmware.
- Connect to the setup AP or LAN IP and open `http://<device-ip>:8080/`.
- Login with the configured admin password. Firmware default is `admin`.
- In `System Setting`, set a unique `Device Name` for each node.
- In `Network Setting`, confirm AP/client WiFi values and device IP.
- In `Broker Setting`, confirm:
  - `Site ID`: `field-a` unless the test case says otherwise.
  - `Topic Prefix`: `site/field-a`.
  - `MQTT Port`: `1883`.
  - `P2P Port`: `4884` or the assigned node-specific port.
  - `Broker Enabled`, `Bridge Enabled`, and `Mesh Enabled` are checked.

Expected status JSON shape:

```json
{
  "status": "ok",
  "peers": 10,
  "wifi_state": "ap",
  "ip_addr": "192.168.4.1",
  "broker_state": "running",
  "p2p_role": "dynamic",
  "connected_peers": 0,
  "remote_subscriptions": 0,
  "last_error": ""
}
```

Values can differ by deployment, but all keys must be present.

## 3-Node Chain

- Configure node 1 with node 2 as an enabled peer.
- Configure node 2 with node 1 and node 3 as enabled peers.
- Configure node 3 with node 2 as an enabled peer.
- Subscribe on node 2 to `site/field-a/data/#`.
- Publish on node 1 to `site/field-a/data/io`; node 2 must receive it.
- Subscribe on node 3 to `site/field-a/data/#`.
- Publish on node 1 to `site/field-a/data/io`; node 3 must receive it through node 2.
- Power off or disconnect node 2.
- Subscribe and publish locally on node 1; local delivery must continue.
- Restart node 2 and repeat the node 2 receive test.

## 10-Node Chain Or Ring

- Configure every node with a connected graph; a full mesh is not required.
- For chain topology, node `N` peers with `N-1` and `N+1` where applicable.
- For ring topology, node 10 also peers back to node 1.
- Subscribe on node 10 to `site/field-a/data/#`.
- Publish on node 1 to `site/field-a/data/io`.
- Node 10 must receive the publish and all broker processes/devices must remain alive.

## Logs To Capture

- Firmware boot log for every node.
- `/status` response before and after peer convergence.
- `/peers` response after configuration.
- Topic Test request payload and result.
- Subscriber output showing the received message.
- Reconnect/restart timestamps and post-restart status output.

## Linux Equivalents

Run these before hardware validation:

```bash
make -C tests/linux test
make -C tests/linux scale-tests scale-ring-tests
```

The scripted equivalents cover:

- 3-node publish routing.
- Node 1 local delivery while node 2 is offline.
- Node 2 restart recovery.
- 10-node chain routing.
- 10-node ring routing.
