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

## 2026-06-26 HTTP/IP follow-up on main

Goal: validate 1-node HTTP first, then 2/3/4 nodes.

Current result: not passed yet. Multiple 1-node runs on `main` start HTTP and
MQTT according to UART, but the Linux host cannot reliably reach the ESP32 over
WiFi.

Evidence:
- `/dev/ttyUSB2` on AP `wlx3c64cf742c7b` and `wlxd84489239707`: UART reports
  STA associated plus HTTP/MQTT listening at `10.88.0.2`, but host ARP becomes
  `INCOMPLETE`; ping, `1883`, and `8080` fail.
- `/dev/ttyUSB3` and `/dev/ttyUSB4` on AP `wlx3c64cf742c7b`: UART reports
  HTTP/MQTT listening. ARP may briefly resolve to the ESP32 MAC, but TCP
  connections to `1883`, `4884`, and `8080` time out or fall back to
  `No route to host`.
- `tcpdump` on `wlx3c64cf742c7b` captured repeated ARP requests from
  `10.88.0.1` for `10.88.0.2` with no reply in the worst case, so several
  failures happen before HTTP is reached.
- Open AP (`LINUX_AP_PASS=`) improved ARP visibility but still timed out on
  TCP, so WPA is not the only factor.
- AP2 `wlxd84489239707` hangs in `nmcli connection up` for this AP profile.

Local changes made during diagnosis:
- `tests/linux/test_wifi_linux_ap_esp32_bridge.sh`
  - Allows `NODE_COUNT=1`.
  - Adds `HTTP_ONLY=1`, `HTTP_CHECK_SECONDS`, and multi-path HTTP soak checks.
  - Makes `nmcli radio wifi on` a warning because this machine sometimes denies
    that operation.
- `../dephy_wifi/src/wifi.c`
  - Starts the STA reconfigure thread only after STA settings are populated.
  - Moves static IPv4 assignment until after STA association, matching DHCP's
    post-association behavior.

Builds/tests run:
- `./scripts/sync_deps.sh replace && ./scripts/build_wifi_bridge_product.sh`
  passed after both WiFi changes.
- 1-node HTTP tmux sessions attempted:
  `http-1node-debug`, `http-1node-debug-ap2-r2`, `http-1node-ttyUSB3`,
  `http-1node-fixed`, `http-1node-openap`,
  `http-1node-static-after-assoc`, and `http-1node-ttyUSB4`.

Next recommended step:
- Compare by flashing the previously passing
  `mqtt_field_bridge_app_wifi_linux_ap_test/build_wifi_bridge_product` image
  onto two boards and running the old 2-node test. If the old image now also
  fails, focus on host AP/driver/NetworkManager state. If the old image passes,
  diff the generated `.config` and linked module sources from the two build
  directories.

## 2026-06-26 UART CLI remote access

For remote development, all six ESP32 UARTs are exposed through host TCP ports
on `192.168.127.5`. Each forwarder runs in tmux through `socat`, so it survives
SSH disconnects. The forwarders are intentionally single-connection listeners
instead of `fork` listeners, because multiple concurrent TCP clients on one UART
can corrupt the interactive CLI stream.

Port mapping:
- `192.168.127.5:17000` -> `/dev/ttyUSB0`
- `192.168.127.5:17001` -> `/dev/ttyUSB1`
- `192.168.127.5:17002` -> `/dev/ttyUSB2`
- `192.168.127.5:17003` -> `/dev/ttyUSB3`
- `192.168.127.5:17004` -> `/dev/ttyUSB4`
- `192.168.127.5:17005` -> `/dev/ttyUSB5`

Connect from a remote shell with:

```sh
nc 192.168.127.5 17003
```

Use `Ctrl-C` to disconnect before reconnecting or before another operator uses
the same port.

Current observation:
- `/dev/ttyUSB2`, `/dev/ttyUSB3`, and `/dev/ttyUSB4` accepted UART writes but
  did not echo CLI output during a short probe.
- `/dev/ttyUSB5` is producing repeated `os: Halting system`, so that board is
  not currently a usable CLI target until reflashed or reset.

## 2026-06-26 CLI-only product follow-up

Reusable CLI UI module:
- Created `/home/judd/moxa/personal/dephy_cli`.
- Pushed to `git@github.com:judadao/dephy_cli.git`.
- Commit: `37234ac feat: add reusable CLI menu renderer`.
- Tag: `dephy-cli-v0.1.0`.
- Validation: `make -f Makefile.linux test` passed with `7 menu checks, 0 failed`.

Product integration:
- `mqtt_field_bridge_app` now depends on `dephy_cli` via `deps.json`.
- Product UART `menu` rendering uses `dephy_cli_render_menu()`.
- Product provisioning Web/HTTP was removed from the build/runtime path:
  no `dephy_web` dependency, no embedded web asset generation, no
  `provisioning_http_start()`, and no `provisioning_http_run()`.
- Product commit pushed: `60aaf50 feat: replace provisioning web with CLI menu module`.

Validation:
- `./scripts/sync_deps.sh replace && make -C tests/linux unit-tests` passed.
- `./scripts/sync_deps.sh replace && ./scripts/build_wifi_bridge_product.sh`
  passed.
- CLI-only WiFi image was flashed to `/dev/ttyUSB2` with `EXIT_CODE=0`.
- Build memory after Web removal:
  - `FLASH: 593760 B / 4194048 B = 14.16%`
  - `dram0_0_seg: 183608 B / 192 KB = 93.39%`
  - `dram1_0_seg: 91472 B / 96 KB = 93.05%`

Hardware note:
- `/dev/ttyUSB2` boots the CLI-only image and no longer starts HTTP.
- UART hardware probing still shows repeated fragments such as
  `console ready. type help`, `menu`, or WiFi log tails depending on NVS/runtime
  state. Clearing storage at `0x3b0000` size `0x30000` removed saved WiFi config,
  but the UART stream still needs cleanup before treating remote `nc` as a
  polished CLI experience.

Update:
- CLI UART output was cleaned up by keeping line-by-line menu rendering and
  increasing the WiFi test profile log buffer to `CONFIG_LOG_BUFFER_SIZE=4096`.
- `/dev/ttyUSB2` was reflashed with the CLI-only image and storage was cleared
  at `0x3b0000` size `0x30000`.
- Hardware CLI validation passed on `/dev/ttyUSB2`:
  - Boot stops at `wifi not configured; waiting for UART CLI provisioning`.
  - `menu` prints all 11 numbered commands without dropped messages.
  - `status` returns `OK network=init ... error=wifi not configured`.
  - `show` returns the saved default network/broker config.
- Remote UART CLI is available at `192.168.127.5:17002`.

Update:
- Added menu return support:
  - `0` returns to the numbered menu.
  - `back` is accepted as a command alias, but the screen text avoids printing
    a bare `back` token to prevent UART output feedback from retriggering the
    menu.
  - `status`, `info`, and `show` end with `0. return to menu`.
- Added `scripts/serial_menu_client.sh` for host-side remote UART access. The
  TCP forwarder can call this helper so a new `nc` connection sends `menu`
  before bridging the UART.
- Reflashed `/dev/ttyUSB2` with the updated CLI-only WiFi image:
  `tmux: build-flash-cli-return-ttyUSB2`, `EXIT_CODE=0`.
- Hardware validation on `/dev/ttyUSB2`:
  - `menu` prints the numbered menu.
  - `1` prints the formatted status table and `0. return to menu`.
  - `0` returns to the numbered menu.
  - `back` returns to the numbered menu.
- Remote validation:
  - `tmux: esp32-cli-ttyUSB2-17002` maps
    `192.168.127.5:17002` to `/dev/ttyUSB2`.
  - `nc 192.168.127.5 17002` immediately shows the CLI menu.

Update:
- Simplified the numbered CLI into three top-level layers:
  - `info`
  - `settings`
  - `system`
- The `info` layer contains `status`, `summary`, and `show`.
- The `settings` layer contains static IP, DHCP, WiFi, broker, broker-state,
  peer, and defaults actions. The broker setup is now visible in the WiFi
  profile menu as `broker <mqtt> <p2p> [ip]`.
- The `system` layer contains reset and reboot.
- Direct text commands such as `status`, `summary`, `show`, `broker ...`,
  `wifi ...`, `reset`, and `reboot` still work without navigating menus.
- Validation:
  - `make -C tests/linux unit_product_console` passed with `88/88`.
  - `./scripts/sync_deps.sh replace && ./scripts/build_wifi_bridge_product.sh`
    passed in `tmux: build-cli-3level-menu`.
  - `/dev/ttyUSB2` was flashed in `tmux: flash-cli-3level-ttyUSB2`,
    `EXIT_CODE=0`.
  - `nc 192.168.127.5 17002` showed the simplified menu; selecting
    `2` then `4` displayed `usage: broker <mqtt> <p2p> [ip]`.
