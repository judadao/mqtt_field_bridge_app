# Build Pipeline — Design Spec

**Date:** 2026-06-19
**Scope:** Get `west build` working end-to-end for the first time.

## Problem

`deps.json` references `minmqtt-v0.1.0` but the broker repo only has tags
`0.9.0.0` and `0.9.1.0`. `sync_deps.sh` fails immediately. Nothing can build.

## Changes

### 1. Broker repo: create `minmqtt-v0.1.0` tag

Tag `HEAD` of `mqtt_min_broker` (commit `477171a`) as an annotated tag
`minmqtt-v0.1.0`. Push the tag to `origin`. This is the first tag in the new
`minmqtt-vX.Y.Z` namespace.

### 2. `scripts/sync_deps.sh` — robustness improvements

Current script uses fragile `sed` heuristics to parse `deps.json`. Replace
with `jq` when available, fall back to the existing `sed` path otherwise.

Additional guards added:

| Guard | Behaviour |
|---|---|
| Tag existence check | `git ls-remote --tags` the broker repo before clone/checkout; fail with a clear error if the tag is not found |
| Dirty check | After cloning, if `deps/mqtt_min_broker` has uncommitted local changes, abort with an error message (prevents silent drift) |
| `--version` flag | Running `./scripts/sync_deps.sh --version` prints the currently pinned broker tag from `deps.json` and exits |

### 3. Run `sync_deps.sh`

After the tag exists, run the script to populate `deps/mqtt_min_broker` at the
correct tag. Verify the checkout matches `deps.json`.

### 4. Verify `west build`

Run `west build` using `scripts/build_product.sh`. If the Zephyr toolchain is
not installed, document the required environment and skip.

## Out of Scope

- NVS config, provisioning HTTP, bridge peer control — next iterations.
- CI integration — tracked in `docs/todo.md`.
- `deps.json` version bump automation — not needed yet.

## Success Criteria

- `./scripts/sync_deps.sh` exits 0 and prints `mqtt_min_broker synced to minmqtt-v0.1.0`.
- `./scripts/sync_deps.sh --version` prints `minmqtt-v0.1.0`.
- `git -C deps/mqtt_min_broker describe --tags` returns `minmqtt-v0.1.0`.
- `west build` exits 0 (or toolchain absence is documented).
