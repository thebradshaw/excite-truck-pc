#!/usr/bin/env bash
# Builds ExciteSetup.exe (Windows) into build/setup/. It installs the Excite PC launcher, which
# then sets each game up from the player's disc.
#
# The exe carries the launcher (ExcitePC.exe, cross-compiled here with MinGW-w64) and, for each
# game, setup/<game>/ (truck, and bots once scripts/excitebots/ is in the checkout): its build
# script plus a payload with the WiiCompiled files as changed by this repo's patches (so the
# player needs no Git), the address table, the project files and the shader set. No game code or
# data is included.
# Needs git, python3, cmake, ninja, mingw-w64 (x86_64-w64-mingw32-g++-posix) and the .NET 8+ SDK.
# ET_LAUNCHER_CMAKE_ARGS passes extra CMake arguments to the launcher build (for example
# -DFETCHCONTENT_SOURCE_DIR_SDL=... for offline builds).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="$REPO/build/setup"
STAGE="$OUTDIR/stage"
WS="$OUTDIR/wiicompiled"
WIICOMPILED_URL=https://github.com/patchzyy/Wiicompiled.git
WIICOMPILED_COMMIT="$(grep -o "WiiCompiledCommit = '[0-9a-f]*'" "$REPO/scripts/build-windows.ps1" | cut -d"'" -f2)"
AURORA_COMMIT="$(grep -o "AuroraCommit = '[0-9a-f]*'" "$REPO/scripts/build-windows.ps1" | cut -d"'" -f2)"

# A clean WiiCompiled tree in $1 with this repo's patches applied, for game $2 (truck or bots).
patch_tree() {
    local ws="$1" game="$2"
    echo "==> Patching a clean WiiCompiled $WIICOMPILED_COMMIT for $game"
    if [ ! -d "$ws/.git" ]; then
        git clone --quiet "$WIICOMPILED_URL" "$ws"
    fi
    git -C "$ws" checkout --quiet --force "$WIICOMPILED_COMMIT"
    git -C "$ws" clean -fdqx
    git -C "$ws" submodule update --init --quiet aurora-main
    git -C "$ws/aurora-main" checkout --quiet --force "$AURORA_COMMIT"
    git -C "$ws/aurora-main" clean -fdqx
    git -C "$ws" apply --whitespace=nowarn "$REPO/patches/0001-excite-truck-runtime.patch"
    git -C "$ws/aurora-main" apply --whitespace=nowarn "$REPO/patches/0002-aurora-channel-colors.patch"
    git -C "$ws" apply --whitespace=nowarn "$REPO/patches/0003-xbox-button-prompts.patch"
    cat "$REPO/tools/sdk_map/et_versions.inc" >> "$ws/runtime/include/guest_abi/guest_versions.inc"
    if [ "$game" = bots ]; then
        git -C "$ws" apply --whitespace=nowarn "$REPO/patches/excitebots/0004-excitebots-runtime.patch"
        cat "$REPO/tools/sdk_map/eb_versions.inc" >> "$ws/runtime/include/guest_abi/guest_versions.inc"
    fi
}

# setup/<game>/ in the stage: the build script, and a payload with the WiiCompiled files the
# patches changed (overlay/), the game's project files, its shader set if there is one, and the
# button prompts. Args: game, patched tree, build script, project folder name, shader set.
stage_game() {
    local game="$1" ws="$2" script="$3" project="$4" cache="$5"
    local dir="$STAGE/setup/$game"
    local overlay="$dir/payload/overlay"
    mkdir -p "$overlay"
    copy() { mkdir -p "$overlay/$(dirname "$1")"; cp "$2/$1" "$overlay/$1"; }
    for f in $(git -C "$ws" diff --name-only; git -C "$ws" ls-files --others --exclude-standard); do
        [ "$f" = aurora-main ] && continue
        [ -f "$ws/$f" ] && copy "$f" "$ws"
    done
    for f in $(git -C "$ws/aurora-main" diff --name-only); do
        mkdir -p "$overlay/aurora-main/$(dirname "$f")"
        cp "$ws/aurora-main/$f" "$overlay/aurora-main/$f"
    done
    find "$overlay" -type d -empty -delete
    mkdir -p "$overlay/projects/$project"
    cp "$REPO/projects/$project"/* "$overlay/projects/$project/"
    local inputs=(overlay prompts)
    if [ -f "$cache" ]; then
        cp "$cache" "$dir/payload/initial_pipeline_cache.db"
        inputs+=(initial_pipeline_cache.db)
    fi
    cp -r "$REPO/assets/prompts" "$dir/payload/prompts"
    cp "$script" "$dir/build-windows.ps1"
    ( cd "$dir/payload" && find "${inputs[@]}" -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16 ) > "$dir/payload/stamp.txt"
    echo "    $game: $(find "$overlay" -type f | wc -l) payload files, stamp $(cat "$dir/payload/stamp.txt")"
}

echo "==> Building the launcher for Windows"
# shellcheck disable=SC2086
cmake -S "$REPO/launcher" -B "$OUTDIR/launcher-win" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/launcher/cmake/mingw-w64.cmake" ${ET_LAUNCHER_CMAKE_ARGS:-} >/dev/null
cmake --build "$OUTDIR/launcher-win"
x86_64-w64-mingw32-strip "$OUTDIR/launcher-win/ExcitePC.exe"

echo "==> Collecting the payloads"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$OUTDIR/launcher-win/ExcitePC.exe" "$STAGE/ExcitePC.exe"
patch_tree "$WS" truck
stage_game truck "$WS" "$REPO/scripts/build-windows.ps1" excite-truck "$REPO/assets/shaders/initial_pipeline_cache.db"
# Excitebots, once its port is in this checkout; until then the launcher shows it as coming soon.
if [ -f "$REPO/scripts/excitebots/build-windows.ps1" ]; then
    patch_tree "$OUTDIR/wiicompiled-bots" bots
    stage_game bots "$OUTDIR/wiicompiled-bots" "$REPO/scripts/excitebots/build-windows.ps1" excitebots \
        "$REPO/assets/shaders/excitebots/initial_pipeline_cache.db"
fi
rm -f "$REPO/setup/payload.zip"
python3 - "$STAGE" "$REPO/setup/payload.zip" <<'PY'
import os, sys, zipfile
stage, out = sys.argv[1:3]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _, files in os.walk(stage):
        for name in sorted(files):
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, stage).replace(os.sep, "/"))
PY

echo "==> Publishing ExciteSetup.exe"
dotnet publish "$REPO/setup/ExciteSetup.csproj" -c Release -o "$OUTDIR/publish" -nologo -v quiet
cp "$OUTDIR/publish/ExciteSetup.exe" "$OUTDIR/ExciteSetup.exe"
rm -f "$REPO/setup/payload.zip"
ls -la "$OUTDIR/ExciteSetup.exe"
