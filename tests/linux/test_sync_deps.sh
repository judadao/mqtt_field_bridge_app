#!/usr/bin/env sh
# test_sync_deps.sh — behavioural tests for scripts/sync_deps.sh
#
# All tests run from the repo root. deps/ may already be populated.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SCRIPT="$ROOT_DIR/scripts/sync_deps.sh"
BROKER_PATH="$ROOT_DIR/deps/mqtt_min_broker"
IO_PATH="$ROOT_DIR/deps/dephy_industrial_io"
TESTKIT_PATH="$ROOT_DIR/deps/dephy_testkit"

PASS=0; FAIL=0

ok()   { PASS=$((PASS+1)); printf '  PASS  %s\n' "$1"; }
fail() { FAIL=$((FAIL+1)); printf '  FAIL  %s\n' "$1"; }

check() {
    label=$1; shift
    if "$@" >/dev/null 2>&1; then ok "$label"; else fail "$label"; fi
}
check_output() {
    label=$1; expected=$2; shift 2
    actual=$("$@" 2>&1) || true
    if printf '%s' "$actual" | grep -qF "$expected"; then
        ok "$label"
    else
        fail "$label (got: $actual)"
    fi
}
check_fail() {
    label=$1; shift
    if "$@" >/dev/null 2>&1; then fail "$label (expected non-zero exit)";
    else ok "$label"; fi
}
check_latest_tag() {
    label=$1
    actual=$(sh "$SCRIPT" --latest 2>&1) || {
        fail "$label (command failed: $actual)"
        return
    }
    case "$actual" in
        minmqtt-v[0-9]*.[0-9]*.[0-9]*) ok "$label" ;;
        *) fail "$label (got: $actual)" ;;
    esac
}
check_latest_report() {
    label=$1
    actual=$(sh "$SCRIPT" --check-latest 2>&1) || {
        fail "$label (command failed: $actual)"
        return
    }
    case "$actual" in
        "mqtt_min_broker up to date: "*|"new mqtt_min_broker version available: "*) ok "$label" ;;
        *) fail "$label (got: $actual)" ;;
    esac
}

echo "=== test_sync_deps.sh ==="

# ── T1: --version prints pinned tag ──────────────────────────────────────
PINNED_VERSION=$(sh "$SCRIPT" --version)
check_output "T1: --version prints pinned tag" "$PINNED_VERSION" \
    sh "$SCRIPT" --version
check_latest_tag "T1b: --latest prints a broker release tag"
check_latest_report "T1c: --check-latest reports current or newer release state"

# ── T2: clean sync exits 0 ───────────────────────────────────────────────
check "T2: sync exits 0" sh "$SCRIPT"
check "T2b: industrial IO dependency synced" test -f "$IO_PATH/repo.json"
check "T2c: testkit dependency synced" test -f "$TESTKIT_PATH/repo.json"

# ── T3: idempotent re-run exits 0 ────────────────────────────────────────
check "T3: second sync exits 0 (idempotent)" sh "$SCRIPT"
check_output "T3b: second sync reports broker cache hit" "mqtt_min_broker already synced" sh "$SCRIPT"

# ── T4: checked-out tag matches deps.json ───────────────────────────────
actual_tag=$(git -C "$BROKER_PATH" describe --tags --exact-match 2>/dev/null || echo "NONE")
pinned_tag=$(sh "$SCRIPT" --version)
if [ "$actual_tag" = "$pinned_tag" ]; then
    ok "T4: checked-out tag matches pinned version"
else
    fail "T4: tag mismatch (checked out=$actual_tag, pinned=$pinned_tag)"
fi

# ── T5: dirty check aborts ────────────────────────────────────────────────
touch "$BROKER_PATH/DIRTY_SYNC_TEST"
check_output "T5: dirty check prints error" "local modifications" \
    sh "$SCRIPT"
check_fail "T5: dirty check exits non-zero" sh "$SCRIPT"
rm "$BROKER_PATH/DIRTY_SYNC_TEST"

# ── T6: clean after dirty cleanup ────────────────────────────────────────
check "T6: sync succeeds after dirty cleanup" sh "$SCRIPT"

# ── T7: missing tag in source repo prints helpful error ──────────────────
# Temporarily patch deps.json to reference a nonexistent tag.
DEPS_JSON="$ROOT_DIR/deps.json"
ORIG=$(cat "$DEPS_JSON")
printf '%s\n' "$ORIG" | sed "s/$PINNED_VERSION/minmqtt-v99.99.99/" > "$DEPS_JSON.tmp"
mv "$DEPS_JSON.tmp" "$DEPS_JSON"
check_output "T7: missing tag prints helpful error" "not found" \
    sh "$SCRIPT"
check_fail "T7: missing tag exits non-zero" sh "$SCRIPT"
printf '%s\n' "$ORIG" > "$DEPS_JSON"   # restore

# ── T8: local replace can switch back to pinned download ──────────────────
check "T8: replace copies local sibling module" sh "$SCRIPT" replace
if [ -f "$BROKER_PATH/zephyr/module.yml" ] && [ ! -d "$BROKER_PATH/.git" ]; then
    ok "T8: replace creates a non-git module copy"
else
    fail "T8: replace did not create expected non-git module copy"
fi
check "T8: download restores pinned git checkout" sh "$SCRIPT" download
actual_tag=$(git -C "$BROKER_PATH" describe --tags --exact-match 2>/dev/null || echo "NONE")
if [ "$actual_tag" = "$PINNED_VERSION" ]; then
    ok "T8: restored checkout matches pinned version"
else
    fail "T8: restored checkout mismatch (checked out=$actual_tag, pinned=$PINNED_VERSION)"
fi

# ── summary ──────────────────────────────────────────────────────────────
echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
