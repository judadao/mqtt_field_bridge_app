# TODO

Source of truth: `docs/todo.yaml`. Update YAML before starting or completing work.

## repo

- [x] Add docs/todo.yaml so the product app is tracked globally.
- [x] Migrate the remaining historical docs/todo.md checklist into structured YAML without losing details.

## integration

- [x] Add dephy_industrial_io as a pinned dependency and wire product IO bridge calls through its public API.

## validation

- [x] Adopt dephy_testkit fixtures for product Linux integration tests and IO/MQTT bridge scenarios.
- [x] Clean up low-risk cppcheck findings in product runtime and provisioning handlers.
- [x] Add and pass an ESP32-to-Linux broker1-5 chain bridge hardware test.

## hardware

- [ ] Flash and provision six W5500 ESP32 boards for manual bridge/MQTT validation. (`tmux: six-eth-config`; management IPs `192.168.127.4`-`.9`, broker IPs `192.168.127.15`-`.20`, MQTT `1883`, bridge `4884`)
- [x] Re-test two W5500 Ethernet ESP32 MQTT bridge peers with current CLI-only firmware. (`tmux: eth-2node-cli-recheck-r6-20260626`; ESP A `192.168.127.15`, ESP B `192.168.127.16`, bidirectional MQTT bridge passed)
- [x] Stage manual validation for 2 ETH, 4 WiFi, then 6 total bridge/MQTT nodes. (`tmux: wifi-4node-bridge-r1-20260626`; WiFi `10.88.0.2`-`.5`; final six-node TCP check passed)
- [x] Validate peer reconnection after WiFi reconnect on target ESP32 hardware. (`obsolete: Ethernet-only product direction`)
- [x] Replace the ESP32 WiFi scan placeholder with live scan results after board-specific WiFi management is selected. (`obsolete: Ethernet-only product direction`)
- [x] Verify DHCP-derived local STA IP, AP/gateway IP, and peer broker IP values on hardware. (`obsolete: Ethernet-only product direction`)

## architecture

- [x] Shift the product direction to Ethernet-only networking.

## firmware

- [x] Make bridge broker targets user-specified instead of WiFi-discovered.
- [x] Remove WiFi/AP firmware code paths from the product app.

## frontend

- [x] Redesign the provisioning web UI for Ethernet and manual broker config.
- [x] Remove provisioning login/auth from the Ethernet-only product UI and API.

## validation

- [x] Update tests and docs for the Ethernet-only product direction.

## performance

- [x] Add fixed-message active broker failure recovery benchmark. (`20260626-fixed-random-failure`; random drop A/B/D; dropped workload: mosquitto `5799/30000`, field no-fallback `5784/30000`, field fallback `29996/30000`)
- [x] Profile provisioning HTML/status rendering and cap response sizes for embedded memory headroom.
- [x] Skip dependency re-sync work when deps.json pins already match clean local deps.
- [x] Redesign random-drop recovery benchmark so clients intended for dropped brokers count against recovery. (`20260626-214321`; full-workload delivery: mosquitto `50.0%`, field no-fallback `50.0%`, field fallback `75.01%`)

## docs

- [x] Make the product README quick start clone-to-shell-to-web instead of a multi-step build flow.
