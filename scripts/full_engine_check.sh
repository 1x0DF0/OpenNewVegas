#!/usr/bin/env bash
# =============================================================================
# OpenNewVegas — one-command full engine check
#
# Runs the complete verification pipeline:
#   1. Clean Release build of ALL targets (warnings captured and reported)
#   2. Full ctest suite on the Release build
#   3. Debug build under ASan+UBSan, every test binary run directly under
#      the sanitizers (any sanitizer report fails the check)
#   4. Walker runtime smoke tests under xvfb (procedural, --demo, and the
#      real-worldspace path driven by the synthetic /tmp fixture ESM)
#
# Exits non-zero on the first failure. Safe to re-run: it removes and
# recreates its own build directories (build_audit_rel / build_audit_asan)
# and never touches build/, build_e2e/, build_rob/ or any other dir.
#
# Requirements: cmake >= 3.16, a C++17 compiler, zlib, SDL2 + OpenGL,
#               xvfb-run (for the headless walker step), python3 (optional,
#               for screenshot pixel-variance verification).
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REL="$ROOT/build_audit_rel"
ASAN="$ROOT/build_audit_asan"
JOBS="${JOBS:-4}"

# All test binaries declared in engine/CMakeLists.txt (kept in sync manually;
# step 2's ctest run will also catch any test added there but missing here).
TEST_BINS=(
  format_tests records_tests nif_tests dds_tests data_files_tests
  scene_tests texture_cache_tests integration_tests robustness_tests
)

step() { printf '\n==== %s ====\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# -----------------------------------------------------------------------------
# Step 1: clean Release build of all targets
# -----------------------------------------------------------------------------
step "1/4 Clean Release build"
rm -rf "$REL"
cmake -B "$REL" "$ROOT/engine" -DCMAKE_BUILD_TYPE=Release

BUILD_LOG="$(mktemp /tmp/onv_check_build.XXXXXX.log)"
cmake --build "$REL" -- "-j$JOBS" 2> "$BUILD_LOG" \
  || { cat "$BUILD_LOG" >&2; fail "Release build failed"; }

WARN_COUNT="$(grep -c 'warning:' "$BUILD_LOG" || true)"
if [ "${WARN_COUNT:-0}" -gt 0 ]; then
  echo "NOTE: $WARN_COUNT compiler warning(s):"
  grep 'warning:' "$BUILD_LOG"
fi
[ -x "$REL/walker" ] || fail "walker did not build (SDL2/OpenGL missing?)"
echo "Release build OK ($WARN_COUNT warnings)."

# -----------------------------------------------------------------------------
# Step 2: full test suite (Release)
# -----------------------------------------------------------------------------
step "2/4 ctest (Release)"
ctest --test-dir "$REL" --output-on-failure || fail "ctest reported failures"

# -----------------------------------------------------------------------------
# Step 3: ASan + UBSan build and direct test-binary run
# -----------------------------------------------------------------------------
step "3/4 Sanitizer build + run (ASan, UBSan)"
rm -rf "$ASAN"
cmake -B "$ASAN" "$ROOT/engine" -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build "$ASAN" --target "${TEST_BINS[@]}" -- "-j$JOBS" \
  || fail "sanitizer build failed"

# halt_on_error makes any UBSan diagnostic fatal so it can't slip past CI.
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
for t in "${TEST_BINS[@]}"; do
  echo "--- $t (sanitized) ---"
  "$ASAN/$t" || fail "$t failed under sanitizers"
done
echo "All ${#TEST_BINS[@]} test binaries clean under ASan/UBSan."

# -----------------------------------------------------------------------------
# Step 4: walker runtime smoke tests (headless)
# -----------------------------------------------------------------------------
step "4/4 Walker runtime (xvfb)"
command -v xvfb-run >/dev/null || fail "xvfb-run not found — cannot run headless walker"

# Verifies a screenshot exists, is plausibly full-size, and (if python3 is
# present) actually contains pixel variation rather than a solid color.
check_ppm() {
  local ppm="$1"
  [ -s "$ppm" ] || fail "screenshot $ppm missing or empty"
  local size; size=$(stat -c%s "$ppm")
  [ "$size" -gt 1000000 ] || fail "screenshot $ppm suspiciously small ($size bytes)"
  if command -v python3 >/dev/null; then
    python3 - "$ppm" <<'PYEOF' || exit 1
import sys
d = open(sys.argv[1], 'rb').read()
body = d[d.index(b'255\n') + 4:]          # skip P6 header
distinct = len(set(body))
if distinct < 8:
    sys.exit(f"FAIL: {sys.argv[1]} has only {distinct} distinct byte values (blank render?)")
print(f"{sys.argv[1]}: {len(body)} pixel bytes, {distinct} distinct values — OK")
PYEOF
  fi
}

# 4a: procedural terrain (no game data found)
rm -f /tmp/onv_check_proc.ppm
xvfb-run -a "$REL/walker" --screenshot /tmp/onv_check_proc.ppm \
  || fail "walker (procedural) exited non-zero"
check_ppm /tmp/onv_check_proc.ppm

# 4b: demo object placement
rm -f /tmp/onv_check_demo.ppm
xvfb-run -a "$REL/walker" --demo --screenshot /tmp/onv_check_demo.ppm \
  || fail "walker (--demo) exited non-zero"
check_ppm /tmp/onv_check_demo.ppm

# 4c: real-worldspace path, using the synthetic ESM that records_tests
# writes to /tmp/onv_fixtures/world.esm (step 3 already ran records_tests,
# but run the Release binary too in case fixtures were cleaned).
"$REL/records_tests" > /dev/null
[ -f /tmp/onv_fixtures/world.esm ] || fail "records_tests did not produce /tmp/onv_fixtures/world.esm"
FNV_ROOT="$(mktemp -d /tmp/onv_check_root.XXXXXX)"
mkdir -p "$FNV_ROOT/Data"
cp /tmp/onv_fixtures/world.esm "$FNV_ROOT/Data/FalloutNV.esm"
rm -f /tmp/onv_check_real.ppm
REAL_LOG="$(mktemp /tmp/onv_check_real.XXXXXX.log)"
ONV_FNV_PATH="$FNV_ROOT" xvfb-run -a "$REL/walker" --screenshot /tmp/onv_check_real.ppm \
  | tee "$REAL_LOG" || fail "walker (real worldspace) exited non-zero"
grep -q 'Loaded real worldspace' "$REAL_LOG" \
  || fail "walker did not take the real-worldspace path (expected 'Loaded real worldspace' line)"
check_ppm /tmp/onv_check_real.ppm
rm -rf "$FNV_ROOT"

printf '\n==== ALL CHECKS PASSED ====\n'
