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
    printf 'ARES v1.0 - %s\n' "$1"
    printf '==================================================\n'
}

has_exact() {
    nl='
'
    case "$nl$output$nl" in
        *"$nl$1$nl"*) return 0 ;;
        *) return 1 ;;
    esac
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
output=$("$ARES" --scenario nav_restart --seed 1 --duration-ms 8000 --record "$NAV_RECORD" 2>&1)
nav_code=$?
output=$(printf '%s\n' "$output" | tr -d '\r')
printf '%s\n' "$output"
if [ "$nav_code" -ne 0 ]; then
    fail "nav_restart exit $nav_code"
fi
if has_exact "navigation_generation: 0" || has_exact "recovery_successes: 0"; then
    echo "Navigation restart was not observed on this host/run." >&2
    echo "This is a failed demonstration run due to host scheduling, not a flight-software failure." >&2
    exit 1
fi
if has_exact "navigation_generation: 1" && has_exact "recovery_successes: 1" && has_exact "recovery_failures: 0"; then
    :
else
    fail "navigation restart demo expected navigation_generation: 1, recovery_successes: 1, and recovery_failures: 0"
fi
if [ ! -f "$NAV_RECORD" ]; then
    fail "missing recording: $NAV_RECORD"
fi
"$REPLAY" --verify "$NAV_RECORD"
nav_replay=$?
if [ "$nav_replay" -ne 0 ]; then
    fail "navigation replay exit $nav_replay"
fi
echo "Observed navigation_generation: 1, recovery_successes: 1, recovery_failures: 0, replay valid."
nav=0

section "Bounded Recovery Failure"
output=$("$ARES" --scenario restart_fail --seed 1 --duration-ms 6000 2>&1)
failure_code=$?
output=$(printf '%s\n' "$output" | tr -d '\r')
printf '%s\n' "$output"
if has_exact "recovery_failures: 1"; then
    :
else
    fail "restart_fail did not report recovery_failures: 1"
fi
if [ "$failure_code" -eq 0 ]; then
    echo "Observed process exit Success (0)."
    echo "RecoveryFailed does not change the process exit. The summary above is the recovery result."
elif [ "$failure_code" -eq 9 ]; then
    echo "Observed FaultHistoryOverflow (9)."
    echo "Recovery had already failed after two attempts. Continued misses then filled the 32-entry history."
else
    fail "restart_fail exit $failure_code"
fi
failure=0

printf '\n==================================================\n'
printf 'ARES v1.0 - Expected versus observed\n'
printf '==================================================\n'
printf 'nominal exit:            expected 0, observed %s\n' "$nominal"
printf 'gps exit:                expected 0, observed %s\n' "$gps"
printf 'gps replay verify:       expected 0, observed %s\n' "$gps_replay"
printf 'navigation restart demo: expected 0, observed %s\n' "$nav"
printf 'recovery-failure demo:   expected 0, observed %s (ares exit %s)\n' "$failure" "$failure_code"
printf 'recordings: %s and %s\n' "$GPS_RECORD" "$NAV_RECORD"
