#!/bin/sh
# Canonical ARES v1.0 demonstration. Uses the real ares and ares-replay binaries.
# Set ARES and REPLAY to installed binaries if they are not under build/release.
# Recordings go to RECORD_DIR, which defaults to <repo>/build.
set -u
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -n "${ARES:-}" ]; then
    ARES=$ARES
elif [ -x "$ROOT/build/release/ares.exe" ]; then
    ARES=$ROOT/build/release/ares.exe
else
    ARES=$ROOT/build/release/ares
fi
if [ -n "${REPLAY:-}" ]; then
    REPLAY=$REPLAY
elif [ -x "$ROOT/build/release/ares-replay.exe" ]; then
    REPLAY=$ROOT/build/release/ares-replay.exe
else
    REPLAY=$ROOT/build/release/ares-replay
fi
RECORD_DIR=${RECORD_DIR:-"$ROOT/build"}
mkdir -p "$RECORD_DIR"
GPS_RECORD=$RECORD_DIR/demo-gps.bin
NAV_RECORD=$RECORD_DIR/demo-nav.bin

fail() {
    echo "demo failed: $1" >&2
    exit 1
}

section() {
    printf '\n==================================================\n'
    printf 'ARES v1.0 — %s\n' "$1"
    printf '==================================================\n'
}

section "Nominal Mission"
"$ARES" --scenario nominal --seed 1 --duration-ms 1000
nominal=$?
[ "$nominal" -eq 0 ] || fail "nominal exit $nominal"

section "GPS Autonomous Recovery"
"$ARES" --scenario gps_stale --seed 42 --duration-ms 12000 --record "$GPS_RECORD"
gps=$?
[ "$gps" -eq 0 ] || fail "gps exit $gps"
"$REPLAY" --verify "$GPS_RECORD"
gps_replay=$?
[ "$gps_replay" -eq 0 ] || fail "gps replay exit $gps_replay"

section "Navigation Autonomous Restart"
sh "$ROOT/scripts/demo_nav_restart.sh"
nav=$?
[ "$nav" -eq 0 ] || fail "navigation restart demonstration exit $nav"

section "Bounded Recovery Failure"
sh "$ROOT/scripts/demo_recovery_failure.sh"
failure=$?
[ "$failure" -eq 0 ] || fail "recovery-failure demonstration exit $failure"

printf '\n==================================================\n'
printf 'ARES v1.0 — Expected versus observed\n'
printf '==================================================\n'
printf 'nominal exit:            expected 0, observed %s\n' "$nominal"
printf 'gps exit:                expected 0, observed %s\n' "$gps"
printf 'gps replay verify:       expected 0, observed %s\n' "$gps_replay"
printf 'navigation restart demo: expected 0, observed %s\n' "$nav"
printf 'recovery-failure demo:   expected 0, observed %s\n' "$failure"
printf 'recordings: %s and %s\n' "$GPS_RECORD" "$NAV_RECORD"
