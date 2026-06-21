# Repository Guidelines

## Project Structure & Module Organization

This repository is the product layer for an MQTT field bridge. Core firmware code lives in `app/src/`, with Zephyr configuration in `app/prj.conf` and `app/CMakeLists.txt`. The provisioning UI source is in `app/web/`; it is embedded into `app/src/generated/provisioning_index.h` by `scripts/build_provisioning_web.js`. Linux host tests and helpers live in `tests/linux/`. Project notes and scenarios are under `docs/`. Dependencies are pinned in `deps.json` and synced into `deps/`; product builds must consume dependency source, headers, and Zephyr module metadata from `deps/`, not arbitrary sibling checkouts.

## Build, Test, and Development Commands

- `./scripts/sync_deps.sh`: sync pinned dependencies into `deps/`.
- `./scripts/sync_deps.sh --check-latest`: check whether a newer broker release exists.
- `./scripts/build_product.sh`: run a Zephyr `west build` for the board from `deps.json`; current default is `esp32_devkitc/esp32/procpu`.
- `make -C tests/linux unit-tests`: build and run host unit tests.
- `make -C tests/linux integration-tests`: build the broker with P2P and run dependency/routing scenarios.
- `make -C tests/linux stress`: run reconnect and throughput stress tests.
- `make -C tests/linux test`: run the main Linux validation suite.
- `make -C tests/linux run-web-server`: serve the provisioning UI at `http://127.0.0.1:8080/` for manual checks.

## Coding Style & Naming Conventions

C code uses C11, 4-space indentation, `snake_case` functions, and module-prefixed public APIs such as `product_config_*` or `bridge_control_*`. Keep headers next to their module in `app/src/`. Prefer small validation helpers and explicit bounds checks for config and HTTP parsing. Web code in `app/web/` uses plain HTML/CSS/JavaScript with no bundler; keep browser logic dependency-free.

## Testing Guidelines

Add or update focused tests in `tests/linux/` for product behavior. Unit test files follow `unit_<module>.c`; shell scenarios use descriptive `test_*.sh` or `stress_*.sh` names. Run `make -C tests/linux unit-tests` before small changes and `make -C tests/linux test` before changes touching peer routing, provisioning HTTP, dependency sync, or shared config behavior. Site-specific web network values belong in ignored `tests/linux/local_web_network.env`.

## Commit & Pull Request Guidelines

Recent commits use short imperative summaries, for example `Refine bridge WiFi provisioning UI` or `Add bridge WiFi provisioning flow`. Keep commits scoped and mention user-visible behavior or tested subsystems. Pull requests should include a concise description, linked issue or motivation, commands run, and screenshots or browser notes for `app/web/` changes.

## Security & Configuration Tips

Do not commit local credentials, IP plans, or generated dependency checkouts. Treat the default `admin` password and provisioning examples as development defaults only. Keep `deps.json` changes intentional and validate them with the Linux suite.
