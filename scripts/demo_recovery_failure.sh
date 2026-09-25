#!/bin/sh
# Bounded recovery-failure demonstration. The wrapper exit means the
# demonstration proved RecoveryFailed, not the raw ares process code.
# Set ARES to an installed binary if it is not under build/release.
set -u
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -n "${ARES:-}" ]; then
    ARES=$ARES
elif [ -x "$ROOT/build/release/ares.exe" ]; then
    ARES=$ROOT/build/release/ares.exe
else
    ARES=$ROOT/build/release/ares
fi

fail() {
    echo "demo failed: $1" >&2
    exit 1
}

output=$("$ARES" --scenario restart_fail --seed 1 --duration-ms 6000 2>&1)
failure=$?
output=$(printf '%s\n' "$output" | tr -d '\r')
printf '%s\n' "$output"
nl='
'
case "$nl$output$nl" in
    *"$nl"'recovery_failures: 1'"$nl"*) ;;
    *) fail "restart_fail did not report recovery_failures: 1" ;;
esac
if [ "$failure" -eq 0 ]; then
    echo "Observed process exit Success (0)."
    echo "RecoveryFailed does not change the process exit. The summary above is the recovery result."
elif [ "$failure" -eq 9 ]; then
    echo "Observed FaultHistoryOverflow (9)."
    echo "Recovery had already failed after two attempts. Continued misses then filled the 32-entry history."
else
    fail "restart_fail exit $failure"
fi
