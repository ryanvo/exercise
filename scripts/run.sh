#!/usr/bin/env bash
# Starts the handler, then the simulator, and waits for both. Extra arguments go to sim.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/release}
"$BIN/handler" &
hpid=$!
sleep 0.3
"$BIN/sim" "$@"
wait "$hpid"
