# WiFi Bridge Test Status

Updated: 2026-06-26 15:40 Asia/Taipei

Current focus:
- Validate two WiFi-only ESP32 boards without W5500 modules before scaling to
  three and four nodes.
- Required behavior: broker bridge end-to-end MQTT delivery plus client
  admission overflow fallback across brokers.

Active tmux session:
- Completed: `bridge-2node-functional`, `bridge-3node-functional`,
  `bridge-4node-functional`.

Current status:
- Script syntax check passed with `bash -n tests/linux/test_wifi_linux_ap_esp32_bridge.sh`.
- Zephyr ESP32 WiFi bridge build completed and produced `zephyr.elf`.
- Linux `mqtt_min_broker` and `mqtt_cli` build completed.
- Linux AP `Linux-Bridge-Test-esp32-bridge` is up on `wlxd84489239707`.
- First run `bridge-2node` failed after `/dev/ttyUSB2` provisioned because the
  host could not reach `http://10.88.0.2:8080/status`; cleanup then stopped the
  AP.
- With AP kept up and `/dev/ttyUSB2` rebooted, `curl
  http://10.88.0.2:8080/status` succeeded and showed broker running.
- Second run `bridge-2node-r2` also failed to get stable HTTP reachability from
  `10.88.0.2`; with `wlx3c64cf742c7b`, neighbor appears but ping has high loss
  and HTTP times out.
- Added ESP32-specific `esp_wifi_set_ps(WIFI_PS_NONE)` fallback in
  `../dephy_wifi_wifi_linux_ap_test/src/wifi.c` after Zephyr WiFi PS disable.
- `bridge-2node-r3` with WPA AP still failed first-node reachability: neighbor
  moved to `FAILED`, MQTT and HTTP returned `No route to host`.
- Added open AP support to `tests/linux/test_wifi_linux_ap_esp32_bridge.sh`.
- Fixed `LINUX_AP_PASS` default handling so an explicitly empty value is
  preserved, and fixed open AP setup to leave `wifi-sec.*` unset.
- Open AP did not fix first-node immediate HTTP reachability.
- Prior known-good `run-20260625-212859.log` passed four nodes with
  `HTTP_STABLE_REQUIRED=0`; current focus is therefore broker bridge and
  fallback, not immediate HTTP stability.
- `bridge-2node-functional` passed with `EXIT_CODE=0`.
- Two-node result:
  - Nodes: `10.88.0.2`, `10.88.0.3`
  - Bidirectional MQTT bridge passed.
  - Fallback load distribution used both brokers:
    `10.88.0.2` handled 2 clients, `10.88.0.3` handled 1 client.
  - Publisher fallback host: `10.88.0.3`.
  - PASS line: `PASS: 2 ESP32 WiFi broker bridge node(s) exchanged MQTT messages on Linux-Bridge-Test`.
- `bridge-3node-functional` passed with `EXIT_CODE=0`.
- Three-node result:
  - Nodes: `10.88.0.2`, `10.88.0.3`, `10.88.0.4`
  - Bidirectional MQTT bridge passed.
  - Fallback load distribution used all three brokers:
    `10.88.0.2` handled 2 clients, `10.88.0.3` handled 2 clients,
    `10.88.0.4` handled 1 client.
  - Publisher fallback host: `10.88.0.4`.
  - PASS line: `PASS: 3 ESP32 WiFi broker bridge node(s) exchanged MQTT messages on Linux-Bridge-Test`.
- `bridge-4node-functional` passed with `EXIT_CODE=0`.
- Four-node result:
  - Nodes: `10.88.0.2`, `10.88.0.3`, `10.88.0.4`, `10.88.0.5`
  - Bidirectional MQTT bridge passed across the chain.
  - Fallback load distribution used all four brokers:
    `10.88.0.2` handled 2 clients, `10.88.0.3` handled 2 clients,
    `10.88.0.4` handled 2 clients, `10.88.0.5` handled 1 client.
  - Publisher fallback host: `10.88.0.5`.
  - PASS line: `PASS: 4 ESP32 WiFi broker bridge node(s) exchanged MQTT messages on Linux-Bridge-Test`.
  - Run log: `tests/linux/out/wifi_linux_ap_bridge/run-20260626-153643.log`.

Changed files:
- `app/prj_wifi_linux_ap.conf`
- `tests/linux/test_wifi_linux_ap_esp32_bridge.sh`
- `deps.json` points this worktree at local WiFi test dependency worktrees.

Next steps:
- Treat immediate HTTP stability as a separate diagnostic; the requested bridge
  and fallback behavior passed for 2, 3, and 4 WiFi-only ESP32 nodes through
  MQTT broker traffic.
- Committed Dephy WiFi power-save change:
  `3208c02 fix: disable esp32 sta power save`.
- Committed product bridge test harness and validation record:
  current HEAD `test: support variable wifi bridge node counts`.
