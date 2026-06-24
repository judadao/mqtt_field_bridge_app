# TODO

## Product direction: Ethernet-only broker bridge

- Completed: product networking is Ethernet-only in the active firmware and UI.
  WiFi STA, SoftAP provisioning, Bridge WiFi scan/join/recent flows, AP-specific
  config fields, login/session auth, and admin password settings were removed
  from the product path.
- Completed: broker bridge targets are user-specified. Users choose broker slot,
  host/IP, MQTT port, P2P port, enabled state, and display name; no discovery is
  required to create bridge peers.
- Completed: tests and docs now cover Ethernet DHCP/static config, manual broker
  peer flows, no-auth APIs, and removed WiFi/login routes.

## ESP32 hardware Wi-Fi validation

- Completed: implement and hardware-test real ESP32 `/wifi/scan` support.
  The Zephyr firmware now calls `NET_REQUEST_WIFI_SCAN` and the hardware test
  captured non-empty scan results through `http://192.168.4.1:8080/wifi/scan`.
  Re-run with `./scripts/hw_esp32_scan_test.sh`; latest hardware run passed with
  16 AP result(s).
- Completed: fix the provisioning UI path that made auth/config failures look
  like a status failure. The web UI now checks public `/status` separately,
  keeps the session token in `sessionStorage`, and only loads `/config` and
  `/peers` after login. Built and flashed to ESP32 on `/dev/ttyUSB0`; UART
  confirmed SoftAP, HTTP 8080, MQTT 1883, and P2P 4884 started.
- Completed: add and run `scripts/hw_esp32_homepage_test.sh` for the explicit
  Linux-AP-off homepage flow. The script saves the active Wi-Fi connection,
  disconnects it, scans for `ESP32-Min-Broker`, connects to the ESP32 SoftAP,
  verifies `http://192.168.4.1:8080/status` and `/`, then restores the previous
  connection. Latest run passed and restored `Hotspot-1`.
- Completed: make the ESP32 SoftAP web UI the primary provisioning path.
  ESP32 starts `ESP32-Min-Broker`, the Linux host connects to it, DHCP assigns
  `192.168.4.x`, and `http://192.168.4.1:8080/` opens reliably after replacing
  the oversized embedded page with a compact single-file web UI.
- Completed: add an end-to-end SoftAP web/API test.
  `scripts/hw_esp32_homepage_test.sh` now verifies DHCP, `/status`, `/`, login,
  `/config`, and `/wifi/scan`, measures response time/size, retries AP connect,
  and restores the previous Linux Wi-Fi connection.
- Completed: optimize ESP32 web page size and runtime memory.
  The ESP32 web UI is 929 bytes and includes Login, Status, Config load/save,
  and WiFi Scan. The full generated UI is no longer included in the Zephyr
  runtime image. Latest product build: FLASH 607,536 B; DRAM0 187,448 B /
  192 KB; DRAM1 69,360 B / 96 KB. Latest hardware run: `/status` 0.029-0.169 s,
  `/` 0.067-0.266 s, `/wifi/scan` returned 12 AP result(s).
- Add a full web-primary feature test suite based on the ESP32 web UI:
  `/`, `/status`, `/login`, `/config`, `/wifi/scan`, `/peers`,
  `/peer-status`, `/broker/control`, `/publish-test`, bridge Wi-Fi endpoints,
  config reset, bad-auth cases, invalid JSON, and persistence after reboot.
- Add browser-level or HTTP-level regression coverage for every primary web UI
  control so the UI and API stay aligned as features change.
- Add safeguards for scanning while SoftAP is active. ESP32 AP and STA share one
  2.4 GHz radio, so scans may need shorter dwell times under load and clear
  UI/API errors if scanning disrupts the provisioning AP connection.
- Debug ESP32 SoftAP association below the product app layer. On 2026-06-24,
  a temporary minimal Zephyr AP-only app with DRAM0 near 51% and heap near 94 KiB
  still advertised BSSID `E0:5A:1B:5A:B4:1D` but Linux USB Wi-Fi association
  timed out even with BSSID pinned, so the remaining failure is likely in the
  Zephyr ESP32 SoftAP driver/radio/client-adapter path rather than the web UI or
  broker footprint alone.
- Add UART command provisioning as the non-web fallback:
  provide serial commands over `/dev/ttyUSB*` for showing status, scanning Wi-Fi,
  setting STA SSID/password, resetting config, and rebooting.
- Add UART provisioning tests:
  flash ESP32, capture serial logs, send provisioning commands over pyserial, and
  verify the resulting config through logs or the HTTP status endpoint.
- Document the two supported provisioning modes:
  SoftAP web UI at `http://192.168.4.1:8080/` and UART command provisioning over
  CP2102 `/dev/ttyUSB*`.
- Stabilize the full single-NIC flow in `scripts/hw_wifi_bridge_test.sh`:
  Linux host connects to the ESP32 SoftAP, writes the Linux hotspot credentials,
  switches the same Wi-Fi NIC into hotspot mode, and waits for ESP32 STA HTTP.
- Fix ESP32 AP+STA channel handling. Current observations show the ESP32 SoftAP
  channel can change while the Linux hotspot is created on another channel, which
  makes the handoff unreliable with one Linux Wi-Fi NIC.
- Improve ESP32 STA reconnect behavior after `/config` writes new Wi-Fi settings.
  The firmware can join a Linux hotspot when the hotspot already exists at boot,
  but the runtime handoff from SoftAP provisioning to STA is not yet stable.
- Report the actual ESP32 STA IP in `/status`. Current runtime status can still
  show the provisioning/AP IP or `0.0.0.0` even when Linux sees the ESP32 STA as
  `10.42.0.x`.
- Add a deterministic factory-reset path for hardware tests. Today a full flash
  erase restores AP-only defaults, but the test script should provide a clean
  reset mode without relying on manual `esptool erase-flash`.
- Add a hardware test mode for two Wi-Fi adapters. One adapter can stay connected
  to the ESP32 SoftAP while the other runs the Linux hotspot; this should avoid
  the single-NIC handoff race.
- Use Linux-AP-only bridge development mode for Codex-side testing:
  Linux keeps Ethernet untouched, starts `Linux-Bridge-Test`, starts the Linux
  MQTT/P2P broker, captures ESP32 UART logs, and waits for ESP32 STA/HTTP/MQTT/P2P.
  A second laptop can connect to the ESP32 SoftAP web UI for live visibility.
- Keep `/dev/ttyACM0` excluded from ESP32 tooling. It is the SEGGER J-Link path;
  ESP32 flash/log/provisioning must use CP2102/CH340 `/dev/ttyUSB*`.

## Module and release workflow

- Audit project dependencies and update reusable modules through the intended
  module-first workflow: update the module repo, run module tests, commit, tag,
  push, then bump the product dependency.
- Sync product dependencies with `./scripts/sync_deps.sh` after module updates
  and verify product build/tests before committing product changes.
- Commit and push completed, tested increments with focused Conventional Commit
  messages.
- Continue iterating through this TODO file: pick the next highest-risk item,
  implement it, run the narrow and relevant full tests, update TODO status, then
  commit and push when the change is complete.
