#!/usr/bin/env bash
# Copies Network Play into a patched WiiCompiled tree: the room library to runtime/netplay and
# the in-game layer plus the game's hooks to runtime/src/netplay. Args: tree, game (truck|bots).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
ws="$1" game="$2"
rm -rf "$ws/runtime/netplay" "$ws/runtime/src/netplay"
mkdir -p "$ws/runtime/netplay" "$ws/runtime/src/netplay"
cp -r "$REPO/netplay/CMakeLists.txt" "$REPO/netplay/include" "$REPO/netplay/src" "$ws/runtime/netplay/"
cp "$REPO/netplay/runtime/netplay_runtime.h" "$REPO/netplay/runtime/netplay_runtime.cpp" "$ws/runtime/src/netplay/"
case "$game" in
    truck) cp "$REPO/netplay/runtime/et_netplay.cpp" "$ws/runtime/src/netplay/" ;;
    bots) cp "$REPO/netplay/runtime/eb_netplay.cpp" "$ws/runtime/src/netplay/" ;;
esac
