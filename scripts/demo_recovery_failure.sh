#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT/build/release/ares.exe" ]; then
    ARES=${ARES:-"$ROOT/build/release/ares.exe"}
else
    ARES=${ARES:-"$ROOT/build/release/ares"}
fi
exec "$ARES" --scenario restart_fail --seed 1 --duration-ms 20000
