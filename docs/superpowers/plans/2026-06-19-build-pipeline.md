# Build Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get `west build` working end-to-end: tag the broker dep, harden `sync_deps.sh`, populate `deps/`, and document the Zephyr toolchain.

**Architecture:** The product app consumes `mqtt_min_broker` as a pinned Zephyr extra module. `deps.json` declares the tag; `sync_deps.sh` clones and checks out that tag into `deps/mqtt_min_broker/`. The product build script then passes that path via `ZEPHYR_EXTRA_MODULES` to `west build`.

**Tech Stack:** POSIX sh, jq (available at `/usr/bin/jq`), git, Zephyr west (not yet installed — Task 4 documents setup)

---

## File Map

| File | Action | Notes |
|------|--------|-------|
| `scripts/sync_deps.sh` | Rewrite | Add jq path, tag existence check, dirty check, `--version` flag |
| `CLAUDE.md` | Modify | Add Zephyr toolchain setup section |
| `/home/judd/moxa/personal/mqtt_min_broker` | Tag | Create + push `minmqtt-v0.1.0` annotated tag (separate repo) |
| `deps/mqtt_min_broker/` | Populated by script | Listed in `.gitignore` — not committed |

---

## Task 1: Tag the broker repo as `minmqtt-v0.1.0`

**Repo:** `/home/judd/moxa/personal/mqtt_min_broker` (separate git repo — all commands run there)

**Files:**
- No files changed — pure git tag operations

- [ ] **Step 1: Create an annotated tag on current HEAD**

```bash
git -C /home/judd/moxa/personal/mqtt_min_broker \
    tag -a minmqtt-v0.1.0 -m "minmqtt-v0.1.0"
```

- [ ] **Step 2: Verify the tag was created locally**

```bash
git -C /home/judd/moxa/personal/mqtt_min_broker tag | grep minmqtt
```

Expected output:
```
minmqtt-v0.1.0
```

- [ ] **Step 3: Verify the tag points to the right commit**

```bash
git -C /home/judd/moxa/personal/mqtt_min_broker rev-list -n1 minmqtt-v0.1.0
```

Expected: the SHA starting with `477171a` (current HEAD).

- [ ] **Step 4: Push the tag to origin**

```bash
git -C /home/judd/moxa/personal/mqtt_min_broker push origin minmqtt-v0.1.0
```

Expected:
```
 * [new tag]         minmqtt-v0.1.0 -> minmqtt-v0.1.0
```

- [ ] **Step 5: Confirm tag is visible on the remote**

```bash
git -C /home/judd/moxa/personal/mqtt_min_broker ls-remote --tags origin | grep minmqtt
```

Expected: one line containing `refs/tags/minmqtt-v0.1.0`.

---

## Task 2: Rewrite `scripts/sync_deps.sh`

**Files:**
- Modify: `scripts/sync_deps.sh`

- [ ] **Step 1: Verify the current script fails (baseline)**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
./scripts/sync_deps.sh --version 2>&1 || true
```

Expected: either prints wrong output or errors (the current script has no `--version` flag).

- [ ] **Step 2: Replace `scripts/sync_deps.sh` with the hardened version**

Overwrite the file with this exact content:

```sh
#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# ---------------------------------------------------------------------------
# JSON parsing: prefer jq, fall back to sed
# ---------------------------------------------------------------------------
if command -v jq >/dev/null 2>&1; then
    BROKER_REPO=$( jq -r '.deps.mqtt_min_broker.repo'    "$ROOT_DIR/deps.json")
    BROKER_VERSION=$(jq -r '.deps.mqtt_min_broker.version' "$ROOT_DIR/deps.json")
    BROKER_PATH=$(  jq -r '.deps.mqtt_min_broker.path'    "$ROOT_DIR/deps.json")
else
    BROKER_REPO=$(   sed -n 's/.*"repo": *"\([^"]*\)".*/\1/p'    "$ROOT_DIR/deps.json" | head -n 1)
    BROKER_VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$ROOT_DIR/deps.json" | head -n 1)
    BROKER_PATH=$(   sed -n 's/.*"path": *"\([^"]*\)".*/\1/p'    "$ROOT_DIR/deps.json" | head -n 1)
fi

# ---------------------------------------------------------------------------
# --version: print pinned broker tag and exit
# ---------------------------------------------------------------------------
if [ "${1:-}" = "--version" ]; then
    printf '%s\n' "$BROKER_VERSION"
    exit 0
fi

if [ -z "$BROKER_REPO" ] || [ -z "$BROKER_VERSION" ] || [ -z "$BROKER_PATH" ]; then
    printf 'error: failed to parse deps.json\n' >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Resolve repo path to absolute (handles ../relative, /absolute, and URLs)
# ---------------------------------------------------------------------------
case "$BROKER_REPO" in
    /*|git@*|https://*)
        BROKER_REPO_ABS="$BROKER_REPO" ;;
    *)
        BROKER_REPO_ABS=$(realpath -m "$ROOT_DIR/$BROKER_REPO") ;;
esac

# ---------------------------------------------------------------------------
# Validate the pinned tag exists in the source repo before doing any work
# ---------------------------------------------------------------------------
if ! git ls-remote --tags "$BROKER_REPO_ABS" "refs/tags/$BROKER_VERSION" 2>/dev/null | grep -q .; then
    printf 'error: tag %s not found in %s\n' "$BROKER_VERSION" "$BROKER_REPO_ABS" >&2
    printf '  Create it: git -C %s tag -a %s -m %s && git -C %s push origin %s\n' \
        "$BROKER_REPO_ABS" "$BROKER_VERSION" "$BROKER_VERSION" \
        "$BROKER_REPO_ABS" "$BROKER_VERSION" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Clone if not yet present
# ---------------------------------------------------------------------------
mkdir -p "$ROOT_DIR/deps"

if [ ! -d "$ROOT_DIR/$BROKER_PATH/.git" ]; then
    git clone "$BROKER_REPO_ABS" "$ROOT_DIR/$BROKER_PATH"
fi

# ---------------------------------------------------------------------------
# Dirty check: refuse to overwrite local modifications
# ---------------------------------------------------------------------------
if [ -n "$(git -C "$ROOT_DIR/$BROKER_PATH" status --porcelain)" ]; then
    printf 'error: %s has local modifications\n' "$BROKER_PATH" >&2
    printf '  Commit, stash, or discard changes before syncing.\n' >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Fetch latest tags and check out the pinned version
# ---------------------------------------------------------------------------
git -C "$ROOT_DIR/$BROKER_PATH" fetch --tags
git -C "$ROOT_DIR/$BROKER_PATH" checkout "$BROKER_VERSION"

printf 'mqtt_min_broker synced to %s\n' "$BROKER_VERSION"
```

- [ ] **Step 3: Make it executable**

```bash
chmod +x /home/judd/moxa/personal/mqtt_field_bridge_app/scripts/sync_deps.sh
```

- [ ] **Step 4: Verify `--version` flag (no network, no deps/ needed)**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
./scripts/sync_deps.sh --version
```

Expected output:
```
minmqtt-v0.1.0
```

- [ ] **Step 5: Verify dirty-check guard (simulate local modifications)**

This test requires `deps/mqtt_min_broker` to already be populated. Skip this step now and run it after Task 3 has completed.

- [ ] **Step 6: Commit the hardened script**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
git add scripts/sync_deps.sh
git commit -m "$(cat <<'EOF'
feat(scripts): harden sync_deps.sh

- Use jq when available; fall back to sed for deps.json parsing
- Validate pinned tag exists in source repo before clone/checkout
- Fail if deps/mqtt_min_broker has local modifications
- Add --version flag to print current pinned broker tag

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Sync deps and verify

**Files:**
- `deps/mqtt_min_broker/` is populated (gitignored — no commit needed)

- [ ] **Step 1: Run the sync script**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
./scripts/sync_deps.sh
```

Expected output:
```
mqtt_min_broker synced to minmqtt-v0.1.0
```

- [ ] **Step 2: Verify the checked-out tag matches deps.json**

```bash
git -C /home/judd/moxa/personal/mqtt_field_bridge_app/deps/mqtt_min_broker \
    describe --tags --exact-match
```

Expected:
```
minmqtt-v0.1.0
```

- [ ] **Step 3: Verify the Zephyr module files are present**

```bash
ls /home/judd/moxa/personal/mqtt_field_bridge_app/deps/mqtt_min_broker/zephyr/
```

Expected: `CMakeLists.txt  Kconfig  module.yml` (or similar — confirms the module dir exists).

- [ ] **Step 4: Run dirty-check guard test (deferred from Task 2 Step 5)**

Touch a file to simulate a local modification, then confirm sync_deps.sh aborts:

```bash
touch /home/judd/moxa/personal/mqtt_field_bridge_app/deps/mqtt_min_broker/DIRTY_TEST
./scripts/sync_deps.sh 2>&1 | head -2
```

Expected output contains:
```
error: deps/mqtt_min_broker has local modifications
```

Then clean up:
```bash
rm /home/judd/moxa/personal/mqtt_field_bridge_app/deps/mqtt_min_broker/DIRTY_TEST
```

- [ ] **Step 5: Confirm clean sync still passes after cleanup**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
./scripts/sync_deps.sh
```

Expected:
```
mqtt_min_broker synced to minmqtt-v0.1.0
```

---

## Task 4: Document Zephyr toolchain in CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

`west` is not currently installed. The broker repo provides `install_env.sh`
which sets up a venv-isolated Zephyr workspace. Add a **Toolchain Setup**
section to CLAUDE.md so future sessions know the one-time install procedure.

- [ ] **Step 1: Add a Toolchain Setup section to CLAUDE.md**

Append the following section after the existing **Build** section:

```markdown
## Toolchain Setup (one-time)

`west` is not in PATH by default. The broker repo provides a setup script:

```bash
# Install Zephyr SDK + west into ~/zephyrproject (isolated venv, no ~/.bashrc changes)
cd /home/judd/moxa/personal/mqtt_min_broker
./install_env.sh

# Activate the Zephyr environment before building
source /home/judd/moxa/personal/mqtt_min_broker/env.sh
```

After sourcing `env.sh`, `west` and the ESP32 cross-compiler are in PATH and
`./scripts/build_product.sh` will work.
```

- [ ] **Step 2: Commit the CLAUDE.md update**

```bash
cd /home/judd/moxa/personal/mqtt_field_bridge_app
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
docs: document Zephyr toolchain setup in CLAUDE.md

west is not in PATH by default; document the one-time install via
mqtt_min_broker/install_env.sh and the env.sh activation step.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Verification Checklist

After all four tasks are complete:

```bash
# 1. Broker tag exists locally and on remote
git -C /home/judd/moxa/personal/mqtt_min_broker tag | grep minmqtt-v0.1.0
git -C /home/judd/moxa/personal/mqtt_min_broker ls-remote --tags origin | grep minmqtt-v0.1.0

# 2. --version flag works
./scripts/sync_deps.sh --version   # → minmqtt-v0.1.0

# 3. deps/ is populated at correct tag
git -C deps/mqtt_min_broker describe --tags --exact-match   # → minmqtt-v0.1.0

# 4. west build (after sourcing env.sh)
source /home/judd/moxa/personal/mqtt_min_broker/env.sh
./scripts/build_product.sh   # → west build succeeds
```
