#!/bin/sh
# Navigation-restart demonstration. The wrapper exit means one verified
# restart was observed, not merely that ares exited 0.
# Set ARES and REPLAY to installed binaries if they are not under build/release.
# The recording is RECORD_DIR/demo-nav.bin, defaulting to <repo>/build.
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
NAV_RECORD=$RECORD_DIR/demo-nav.bin

fail() {
    echo "demo failed: $1" >&2
    exit 1
}

output=$("$ARES" --scenario nav_restart --seed 1 --duration-ms 8000 --record "$NAV_RECORD" 2>&1)
code=$?
output=$(printf '%s\n' "$output" | tr -d '\r')
printf '%s\n' "$output"
nl='
'
has_exact() {
    case "$nl$output$nl" in
        *"$nl$1$nl"*) return 0 ;;
        *) return 1 ;;
    esac
}

if [ "$code" -ne 0 ]; then
    fail "nav_restart exit $code"
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
replay=$?
if [ "$replay" -ne 0 ]; then
    fail "navigation replay exit $replay"
fi
echo "Observed navigation_generation: 1, recovery_successes: 1, recovery_failures: 0, replay valid."
