#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT/build/release/ares.exe" ]; then
    ARES=${ARES:-"$ROOT/build/release/ares.exe"}
    REPLAY=${REPLAY:-"$ROOT/build/release/ares-replay.exe"}
else
    ARES=${ARES:-"$ROOT/build/release/ares"}
    REPLAY=${REPLAY:-"$ROOT/build/release/ares-replay"}
fi
mkdir -p "$ROOT/build"
RECORD=${RECORD:-"$ROOT/build/demo-gps.bin"}
"$ARES" --scenario gps_stale --seed 42 --duration-ms 12000 --record "$RECORD"
"$REPLAY" "$RECORD"
exec "$REPLAY" --verify "$RECORD"
