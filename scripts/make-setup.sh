#!/usr/bin/env bash
# Builds ExciteTruckSetup.exe (Windows, drag-and-drop setup) into build/setup/.
#
# The exe carries scripts/build-windows.ps1 plus a payload: the WiiCompiled files as changed by
# this repo's patches (so the player needs no Git), the address table, the project files and
# the shader set. No game code or data is included. Needs git, python3 and the .NET 8+ SDK.
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
cp -r "$REPO/launcher" "$WS/runtime/et_launcher"
printf '\nadd_subdirectory(et_launcher)\n' >> "$WS/runtime/CMakeLists.txt"

echo "==> Collecting the payload"
rm -rf "$STAGE"
mkdir -p "$STAGE/payload/overlay"
copy() { mkdir -p "$STAGE/payload/overlay/$(dirname "$1")"; cp "$2/$1" "$STAGE/payload/overlay/$1"; }
for f in $(git -C "$WS" diff --name-only; git -C "$WS" ls-files --others --exclude-standard); do
    [ "$f" = aurora-main ] && continue
    [ -f "$WS/$f" ] && copy "$f" "$WS"
done
for f in $(git -C "$WS/aurora-main" diff --name-only); do
    copy "$f" "$WS/aurora-main"; mkdir -p "$STAGE/payload/overlay/aurora-main/$(dirname "$f")"
    mv "$STAGE/payload/overlay/$f" "$STAGE/payload/overlay/aurora-main/$f"
done
find "$STAGE/payload/overlay" -type d -empty -delete
mkdir -p "$STAGE/payload/overlay/projects/excite-truck"
cp "$REPO"/projects/excite-truck/* "$STAGE/payload/overlay/projects/excite-truck/"
cp "$REPO/assets/shaders/initial_pipeline_cache.db" "$STAGE/payload/"
cp -r "$REPO/assets/prompts" "$STAGE/payload/prompts"
cp "$REPO/scripts/build-windows.ps1" "$STAGE/"
( cd "$STAGE/payload" && find overlay initial_pipeline_cache.db prompts -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16 ) > "$STAGE/payload/stamp.txt"
echo "    $(find "$STAGE/payload/overlay" -type f | wc -l) changed WiiCompiled files, stamp $(cat "$STAGE/payload/stamp.txt")"
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

echo "==> Publishing ExciteTruckSetup.exe"
dotnet publish "$REPO/setup/ExciteTruckSetup.csproj" -c Release -o "$OUTDIR/publish" -nologo -v quiet
cp "$OUTDIR/publish/ExciteTruckSetup.exe" "$OUTDIR/ExciteTruckSetup.exe"
rm -f "$REPO/setup/payload.zip"
ls -la "$OUTDIR/ExciteTruckSetup.exe"
