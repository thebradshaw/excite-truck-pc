#!/usr/bin/env bash
# Builds ExciteSetup.exe (Windows) into build/setup/. It installs the Excite PC launcher, which
# then sets each game up from the player's disc.
#
# The exe carries the launcher (ExcitePC.exe, cross-compiled here with MinGW-w64) and, for Excite
# Truck, setup/truck/: scripts/build-windows.ps1 plus a payload with the WiiCompiled files as
# changed by this repo's patches (so the player needs no Git), the address table, the project
# files and the shader set. No game code or data is included.
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

echo "==> Patching a clean WiiCompiled $WIICOMPILED_COMMIT"
if [ ! -d "$WS/.git" ]; then
    git clone --quiet "$WIICOMPILED_URL" "$WS"
fi
git -C "$WS" checkout --quiet --force "$WIICOMPILED_COMMIT"
git -C "$WS" clean -fdqx
git -C "$WS" submodule update --init --quiet aurora-main
git -C "$WS/aurora-main" checkout --quiet --force "$AURORA_COMMIT"
git -C "$WS/aurora-main" clean -fdqx
git -C "$WS" apply --whitespace=nowarn "$REPO/patches/0001-excite-truck-runtime.patch"
git -C "$WS/aurora-main" apply --whitespace=nowarn "$REPO/patches/0002-aurora-channel-colors.patch"
git -C "$WS" apply --whitespace=nowarn "$REPO/patches/0003-xbox-button-prompts.patch"
cat "$REPO/tools/sdk_map/et_versions.inc" >> "$WS/runtime/include/guest_abi/guest_versions.inc"

echo "==> Building the launcher for Windows"
# shellcheck disable=SC2086
cmake -S "$REPO/launcher" -B "$OUTDIR/launcher-win" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/launcher/cmake/mingw-w64.cmake" ${ET_LAUNCHER_CMAKE_ARGS:-} >/dev/null
cmake --build "$OUTDIR/launcher-win"
x86_64-w64-mingw32-strip "$OUTDIR/launcher-win/ExcitePC.exe"

echo "==> Collecting the payload"
rm -rf "$STAGE"
TRUCK="$STAGE/setup/truck"
mkdir -p "$TRUCK/payload/overlay"
cp "$OUTDIR/launcher-win/ExcitePC.exe" "$STAGE/ExcitePC.exe"
copy() { mkdir -p "$TRUCK/payload/overlay/$(dirname "$1")"; cp "$2/$1" "$TRUCK/payload/overlay/$1"; }
for f in $(git -C "$WS" diff --name-only; git -C "$WS" ls-files --others --exclude-standard); do
    [ "$f" = aurora-main ] && continue
    [ -f "$WS/$f" ] && copy "$f" "$WS"
done
for f in $(git -C "$WS/aurora-main" diff --name-only); do
    copy "$f" "$WS/aurora-main"; mkdir -p "$TRUCK/payload/overlay/aurora-main/$(dirname "$f")"
    mv "$TRUCK/payload/overlay/$f" "$TRUCK/payload/overlay/aurora-main/$f"
done
find "$TRUCK/payload/overlay" -type d -empty -delete
mkdir -p "$TRUCK/payload/overlay/projects/excite-truck"
cp "$REPO"/projects/excite-truck/* "$TRUCK/payload/overlay/projects/excite-truck/"
cp "$REPO/assets/shaders/initial_pipeline_cache.db" "$TRUCK/payload/"
cp -r "$REPO/assets/prompts" "$TRUCK/payload/prompts"
cp "$REPO/scripts/build-windows.ps1" "$TRUCK/"
( cd "$TRUCK/payload" && find overlay initial_pipeline_cache.db prompts -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16 ) > "$TRUCK/payload/stamp.txt"
echo "    $(find "$TRUCK/payload/overlay" -type f | wc -l) changed WiiCompiled files, stamp $(cat "$TRUCK/payload/stamp.txt")"
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
