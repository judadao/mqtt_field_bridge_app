#!/usr/bin/env sh
# test_sync_deps.sh — behavioural tests for scripts/sync_deps.sh
#
# All tests run from the repo root. deps/ may already be populated.
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SCRIPT="$ROOT_DIR/scripts/sync_deps.sh"
BROKER_PATH="$ROOT_DIR/deps/mqtt_min_broker"

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

echo "=== test_sync_deps.sh ==="

# ── T1: --version prints pinned tag ──────────────────────────────────────
PINNED_VERSION=$(sh "$SCRIPT" --version)
check_output "T1: --version prints pinned tag" "$PINNED_VERSION" \
    sh "$SCRIPT" --version
check_output "T1b: --latest prints current broker tag" "$PINNED_VERSION" \
    sh "$SCRIPT" --latest
check_output "T1c: --check-latest reports up to date" "up to date: $PINNED_VERSION" \
    sh "$SCRIPT" --check-latest

# ── T2: clean sync exits 0 ───────────────────────────────────────────────
check "T2: sync exits 0" sh "$SCRIPT"

# ── T3: idempotent re-run exits 0 ────────────────────────────────────────
check "T3: second sync exits 0 (idempotent)" sh "$SCRIPT"

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

# ── summary ──────────────────────────────────────────────────────────────
echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
