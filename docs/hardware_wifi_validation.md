# Hardware Wi-Fi Validation

Use these scripts when validating the ESP32 product app against the local Linux
Wi-Fi adapter. The scripts intentionally fail on missing real Wi-Fi behavior so
UART logs can be used for debugging.

## Build, Flash, And Capture Logs

```sh
./scripts/hw_esp32_flash_monitor.sh
```

Defaults:

- ESP32 serial is auto-detected from CP210x/CH340-style `/dev/ttyUSB*` ports.
- `/dev/ttyACM0` is not used because it is the SEGGER J-Link interface on this
  machine.
- `west` is resolved from `deps/dephy/zephyrproject`, the sibling local
  `../dephy/zephyrproject`, or `PATH`.
- Zephyr output defaults to `build_product/`; override it with `BUILD_DIR=...`.
- Build and flash run before UART capture.
- Logs are written under `tests/linux/out/hardware/`.

Useful overrides:

```sh
SERIAL_PORT=/dev/ttyUSB0 LOG_SECONDS=90 ./scripts/hw_esp32_flash_monitor.sh
SKIP_BUILD=1 SKIP_FLASH=1 ./scripts/hw_esp32_flash_monitor.sh
```

## UART Console Control

The ESP32 firmware starts a lightweight UART command console on the CP210x-style
`/dev/ttyUSB*` port. `/dev/ttyACM0` is intentionally not used on this bench
because it is the SEGGER J-Link interface.

Run one command and print the response:

```sh
./scripts/hw_esp32_console.sh status
./scripts/hw_esp32_console.sh show
./scripts/hw_esp32_console.sh scan
```

On CP210x auto-reset boards, opening the serial port for a one-shot command may
reset the ESP32. The script waits for the firmware console to become ready
before sending the command. For iterative development, keep one interactive
session open.

Open an interactive line-oriented console:

```sh
./scripts/hw_esp32_console.sh
```

Supported commands:

- `help`
- `status`
- `show`
- `scan`
- `wifi <ssid> <password>`: save STA Wi-Fi and request reconnect.
- `clear-wifi`: clear STA Wi-Fi and return to AP-only config.
- `ap <ssid> <password>`: save ESP32 SoftAP settings; reboot to apply.
- `broker <mqtt_port> <p2p_port>`: save broker ports; reboot to apply.
- `peer <index> <name> <host> [mqtt_port] [p2p_port] [0|1]`
- `defaults small|large`
- `reset`
- `reboot`

## Single-Wi-Fi ESP32 AP Homepage Test

Use this as the default development check for the ESP32 provisioning UI. The
Linux host uses one Wi-Fi adapter, disconnects it from any current Wi-Fi/AP
profile, scans for `ESP32-Min-Broker`, connects to the ESP32 SoftAP, validates
`/status`, `/`, login, `/config`, and `/wifi/scan`, then restores the previous
Wi-Fi connection by default.

```sh
./scripts/hw_esp32_homepage_test.sh
```

Defaults:

- ESP32 AP: `ESP32-Min-Broker` / `12345678`
- ESP32 HTTP: `http://192.168.4.1:8080`
- ESP32 NetworkManager profile: `ESP32-Min-Broker-test`
- Previous Wi-Fi connection is restored on exit.

Useful overrides:

```sh
WIFI_IFACE=wlx3c64cf742c7b ./scripts/hw_esp32_homepage_test.sh
RESTORE_WIFI=0 WIFI_IFACE=wlx3c64cf742c7b ./scripts/hw_esp32_homepage_test.sh
RESTORE_WIFI=0 ./scripts/hw_esp32_homepage_test.sh
SCAN_RETRIES=12 WAIT_SECONDS=90 ./scripts/hw_esp32_homepage_test.sh
ITERATIONS=3 ./scripts/hw_esp32_homepage_test.sh
```

The two-adapter bench mode is still available for stress/debug runs, but do not
use it as the default pass/fail path because NetworkManager, `wpa_supplicant`,
and Realtek USB Wi-Fi drivers can interfere with concurrent AP and STA scans:

```sh
AP_WIFI_IFACE=wlx3c64cf742c7b ESP32_WIFI_IFACE=wlxd84489239707 \
    ./scripts/hw_esp32_homepage_test.sh
```

The ESP32 Zephyr image serves a compact single-file web UI at `/` to avoid
large-response stalls on the ESP32 Wi-Fi socket path. It includes browser
controls for Login, Status, Config load/save, and WiFi Scan. The full development
UI remains available in host-side tests, but the ESP32 runtime page is optimized
for reliable SoftAP provisioning.

Latest measured ESP32 run:

- Homepage size: 3,548 bytes.
- `/status` time: 0.097 s.
- `/` time: 2.359 s on the first post-flash run.
- Authenticated `/config` passed.
- Authenticated `/wifi/scan` passed with 8 AP result(s).
- Product build memory: FLASH 612,208 B; DRAM0 188,104 B / 192 KB; DRAM1
  72,432 B / 96 KB.

```sh
ESP32_AP_SSID=ESP32-Min-Broker ESP32_AP_PASS=12345678 \
    ./scripts/hw_wifi_bridge_test.sh
```

The test flow is:

1. Enable the Linux Wi-Fi radio and scan for the ESP32 AP.
2. Connect Linux to the ESP32 AP.
3. Login to the ESP32 HTTP API and capture `/status` plus `/wifi/scan`.
4. Configure the ESP32 station SSID/password to a Linux-hosted hotspot.
5. Start the Linux hotspot with NetworkManager.
6. Wait for the ESP32 to appear on the Linux hotspot and answer HTTP.

If step 6 fails, inspect the UART log from `hw_esp32_flash_monitor.sh`.

## Linux AP + Broker Bridge Development Mode

Use this mode when Codex should not use the Linux Wi-Fi adapter to view the
ESP32 web UI. The Linux host only opens an AP and broker; a second laptop can
connect to the ESP32 SoftAP to inspect the web UI.

```sh
./scripts/hw_linux_ap_broker_bridge_test.sh
```

By default this starts both:

- Linux AP: `Linux-Bridge-Test` / `bridge1234`
- Linux broker: auto-selected `10.77.0.1`-`10.99.0.1` subnet, port `1883`
  MQTT and `4884` P2P

For the two-adapter bench setup, which is an advanced stress/debug path:

```sh
AP_WIFI_IFACE=wlx3c64cf742c7b ESP32_WIFI_IFACE=wlxd84489239707 \
    ./scripts/hw_linux_ap_broker_bridge_test.sh
```

To only bring up the Linux AP + broker environment and not wait for ESP32 yet:

```sh
WAIT_ESP32=0 ./scripts/hw_linux_ap_broker_bridge_test.sh
```

The test flow is:

1. Build and start the Linux MQTT/P2P broker from `deps/mqtt_min_broker/`.
2. Start `Linux-Bridge-Test` on the Linux Wi-Fi adapter.
3. Leave existing Ethernet connections untouched.
4. Optionally reset/flash ESP32 and capture UART logs with
   `RESET_ESP32=1 ./scripts/hw_linux_ap_broker_bridge_test.sh`.
5. Wait for ESP32 to join the Linux AP and answer HTTP, MQTT, and P2P probes.

Expected ESP32 settings:

- `wifi_ssid=Linux-Bridge-Test`
- `wifi_password=bridge1234`
- Linux broker/gateway host is printed by the script. It defaults to an
  available `10.77.0.1`-`10.99.0.1` subnet so stale NetworkManager `dnsmasq`
  processes cannot block AP startup.
- ESP32 may keep `ESP32-Min-Broker` enabled for the second laptop.

Useful overrides:

```sh
LINUX_AP_SSID=Linux-Bridge-Test LINUX_AP_PASS=bridge1234 \
RESET_ESP32=1 WAIT_SECONDS=180 \
    ./scripts/hw_linux_ap_broker_bridge_test.sh
```

By default, this mode leaves the Linux AP and Linux broker running after the
script exits so a second laptop can continue observing the ESP32 web UI while
Codex inspects logs and probes broker state. Use `KEEP_AP=0 KEEP_BROKER=0` for a
self-cleaning run.

Stop the broker when finished:

```sh
./scripts/hw_linux_ap_broker_stop.sh
```

Stop both the broker and the Linux hotspot:

```sh
STOP_AP=1 ./scripts/hw_linux_ap_broker_stop.sh
```
