#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT/build/release/ares.exe" ]; then
    ARES=${ARES:-"$ROOT/build/release/ares.exe"}
else
    ARES=${ARES:-"$ROOT/build/release/ares"}
fi
exec "$ARES" --scenario gps_stale --seed 42 --duration-ms 12000
