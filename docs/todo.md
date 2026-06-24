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

## hardware

- [ ] Validate peer reconnection after WiFi reconnect on target ESP32 hardware. (`blocked`)
- [ ] Replace the ESP32 WiFi scan placeholder with live scan results after board-specific WiFi management is selected. (`blocked`)
- [ ] Verify DHCP-derived local STA IP, AP/gateway IP, and peer broker IP values on hardware. (`blocked`)

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

- [x] Profile provisioning HTML/status rendering and cap response sizes for embedded memory headroom.
- [x] Skip dependency re-sync work when deps.json pins already match clean local deps.

## docs

- [x] Make the product README quick start clone-to-shell-to-web instead of a multi-step build flow.
