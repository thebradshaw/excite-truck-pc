#!/usr/bin/env bash
# One-step build of Excite Truck PC on Linux, from your own disc image.
#
#   scripts/build-linux.sh "/path/to/Excite Truck (USA).wbfs"
#
# Accepts a .wbfs/.iso/.rvz/.ciso/.wia disc image, a .zip holding one (or the first part,
# .zip.001, of a zip split by 7-Zip), or a folder already extracted with nodtool
# (the one holding sys/ and files/). The result is a self-contained folder, ExciteTruck/ in the
# repo root, with the game, its settings (ExciteTruck/UserData) and your Music folder.
# The Excite PC launcher (scripts/build-launcher.sh) runs this for you with a progress bar.
#
# Needs: git, cmake, ninja, clang/clang++, python3, curl and the .NET 8 SDK (or newer).
# Debian/Ubuntu: sudo apt install git cmake ninja-build clang python3 curl dotnet-sdk-8.0
#
# Environment (all optional):
#   OUT=dir          where to put the game (default: <repo>/ExciteTruck)
#   WORK=dir         scratch space, ~15 GB (default: <repo>/build/work)
#   JOBS=n           parallel compile jobs (default: all cores)
#   ET_CMAKE_ARGS    extra CMake arguments, e.g. FETCHCONTENT_SOURCE_DIR_* for offline builds
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DISC="${1:-}"
OUT="${OUT:-$REPO/ExciteTruck}"
WORK="${WORK:-$REPO/build/work}"
JOBS="${JOBS:-$(nproc)}"

WIICOMPILED_URL=https://github.com/patchzyy/Wiicompiled.git
WIICOMPILED_COMMIT=4993b2226c8f24defe84fadf04e69732c635a90d   # v0.9.2
AURORA_COMMIT=3c80a1a9e8d75c61d31a903c6a3cef5f54c02f33
NODTOOL_URL=https://github.com/encounter/nod/releases/download/v1.4.4/nodtool-linux-x86_64
NODTOOL_SHA256=cf20828d1a437ca14ed7acfd5545e1a0e62c082f5a77240a399aa4f172119fc4
DOL_SHA256=573b5dcf48ebe0e2fba11c5bbcd88cb149696924fea7ed0f98c78986671e76b6

# A step's title, and where the launcher's progress bar stands when it starts ("A B": the steps
# after it run from A to B percent, moved along by ninja's [n/m] lines).
step() { printf '\n==> %s\n' "$1"; if [ -n "${2:-}" ]; then printf '##progress %s\n' "$2"; fi; }
die() { printf '\nError: %s\n' "$*" >&2; exit 1; }

[ -n "$DISC" ] || die "usage: $0 <disc image or extracted folder>"
[ -e "$DISC" ] || die "not found: $DISC"
DISC="$(cd "$(dirname "$DISC")" && pwd)/$(basename "$DISC")"

step "Checking tools" 1
for tool in git cmake ninja clang clang++ python3 curl dotnet; do
    command -v "$tool" >/dev/null || die "$tool is not installed (see the top of this script)"
done
dotnet --list-sdks | grep -Eq '^([89]|[1-9][0-9])\.' || die "the .NET 8 SDK (or newer) is not installed"
mkdir -p "$WORK" "$OUT"

# --- Disc ---------------------------------------------------------------------------------
DATA="$OUT/Disc"
if [ -f "$DISC/sys/main.dol" ]; then
    DATA="$DISC"
elif [ ! -f "$DATA/sys/main.dol" ]; then
    NODTOOL="$(command -v nodtool || true)"
    if [ -z "$NODTOOL" ]; then
        NODTOOL="$WORK/tools/nodtool"
        if [ ! -x "$NODTOOL" ]; then
            step "Downloading nodtool (reads Wii disc images)" 7
            mkdir -p "$WORK/tools"
            curl -fsSL -o "$NODTOOL.partial" "$NODTOOL_URL"
            echo "$NODTOOL_SHA256  $NODTOOL.partial" | sha256sum -c --quiet || die "nodtool download is corrupt"
            chmod +x "$NODTOOL.partial" && mv "$NODTOOL.partial" "$NODTOOL"
        fi
    fi
    IMAGE="$DISC"
    case "$DISC" in
        *.zip.[0-9][0-9][0-9]|*.zip)
            step "Unpacking the zip" 5
            ZIPDIR="$WORK/disc-zip"
            rm -rf "$ZIPDIR"; mkdir -p "$ZIPDIR"
            ZIP="$DISC"
            case "$DISC" in *.zip.[0-9][0-9][0-9])
                # A zip split into parts by 7-Zip: join name.zip.001, .002, ... back together.
                ZIP="$ZIPDIR/disc.zip"; cat "${DISC%.*}".[0-9][0-9][0-9] > "$ZIP" ;;
            esac
            python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$ZIPDIR/out"
            IMAGE="$(find "$ZIPDIR/out" -type f \( -iname '*.wbfs' -o -iname '*.iso' -o -iname '*.rvz' -o -iname '*.ciso' -o -iname '*.wia' -o -iname '*.gcz' \) | head -1)"
            [ -n "$IMAGE" ] || die "the zip holds no .wbfs/.iso/.rvz disc image"
            ;;
    esac
    step "Extracting the disc" 8
    rm -rf "$DATA.partial"
    "$NODTOOL" extract "$IMAGE" "$DATA.partial"
    rm -rf "$WORK/disc-zip"
    mv "$DATA.partial" "$DATA"
fi
[ -f "$DATA/sys/main.dol" ] || die "no sys/main.dol in $DATA"
echo "$DOL_SHA256  $DATA/sys/main.dol" | sha256sum -c --quiet ||
    die "this is not the US Excite Truck disc (REXE01, revision 0); other versions aren't supported yet"

# --- WiiCompiled, patched for Excite Truck ------------------------------------------------
WS="$WORK/wiicompiled"
PATCH_STAMP="$(cat "$REPO"/patches/*.patch "$REPO/tools/sdk_map/et_versions.inc" "$REPO"/projects/excite-truck/* |
    sha256sum | cut -c1-16)"
if [ "$(cat "$WS/.et-patched" 2>/dev/null)" != "$PATCH_STAMP" ]; then
    step "Fetching WiiCompiled (the recompiler and runtime this port is built on)" 12
    if [ ! -d "$WS/.git" ]; then
        rm -rf "$WS"
        git clone --quiet "$WIICOMPILED_URL" "$WS"
    fi
    git -C "$WS" fetch --quiet origin "$WIICOMPILED_COMMIT" 2>/dev/null || true
    git -C "$WS" checkout --quiet --force "$WIICOMPILED_COMMIT"
    git -C "$WS" clean -fdq -e build -e generated -e Assets
    git -C "$WS" submodule update --init --quiet aurora-main
    git -C "$WS/aurora-main" checkout --quiet --force "$AURORA_COMMIT"
    git -C "$WS/aurora-main" clean -fdq

    step "Applying the Excite Truck patches" 15
    git -C "$WS" apply --whitespace=nowarn "$REPO/patches/0001-excite-truck-runtime.patch"
    git -C "$WS/aurora-main" apply --whitespace=nowarn "$REPO/patches/0002-aurora-channel-colors.patch"
    git -C "$WS" apply --whitespace=nowarn "$REPO/patches/0003-xbox-button-prompts.patch"
    cat "$REPO/tools/sdk_map/et_versions.inc" >> "$WS/runtime/include/guest_abi/guest_versions.inc"
    mkdir -p "$WS/projects/excite-truck"
    cp "$REPO"/projects/excite-truck/* "$WS/projects/excite-truck/"
    echo "$PATCH_STAMP" > "$WS/.et-patched"
fi

# --- Translate the game's code to C++ -----------------------------------------------------
mkdir -p "$WS/Assets"
cp "$DATA/sys/main.dol" "$WS/Assets/main.dol"
CLI="$WS/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
if [ ! -f "$CLI" ]; then
    step "Building the translator" 20
    dotnet build "$WS/translator/src/Translator.Cli/Translator.Cli.csproj" -c Release -v quiet -nologo
fi
P=projects/excite-truck/recomp.yml
cd "$WS"
step "Translating the game's code (a few minutes)" 25
dotnet "$CLI" translate-recursive 0x80004170 --project $P --outdir generated/functions \
    --output-metadata generated/base_translation_output.json \
    --production-source-bundle generated/base_translation_sources.bin \
    --no-function-files --prune-stale --threads "$JOBS"
dotnet "$CLI" generate-data-init --project $P
dotnet "$CLI" emit-build-shards --project $P --base-metadata generated/base_translation_output.json \
    --base-functions-dir generated/functions --native-source-dir runtime/src --out generated/build_shards

# --- Compile ------------------------------------------------------------------------------
step "Configuring" 34
# shellcheck disable=SC2086
cmake -S runtime -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAURORA_SDL3_PROVIDER=vendor \
    -DMKW_INITIAL_PIPELINE_CACHE="$REPO/assets/shaders/initial_pipeline_cache.db" \
    -DMKW_TRANSLATED_COMPILE_JOBS="$JOBS" ${ET_CMAKE_ARGS:-} >/dev/null
step "Compiling (this is the long part: 20 to 60 minutes)" "40 97"
cmake --build build-linux --target WiiCompiled --parallel "$JOBS"

# --- Package ------------------------------------------------------------------------------
step "Installing to $OUT" 98
B="$WS/build-linux"
install -m 755 "$B/WiiCompiled" "$OUT/ExciteTruck"
rm -f "$OUT/ExciteTruckLauncher"   # older builds put a launcher here; it now lives on its own
for f in "$B"/*.bin "$B"/*.pem "$B"/*.db "$B"/*.so*; do [ -e "$f" ] && cp -f "$f" "$OUT/"; done
for d in fonts wii_bootstrap; do [ -d "$B/$d" ] && rm -rf "${OUT:?}/$d" && cp -r "$B/$d" "$OUT/"; done
# Xbox button prompts (our own art, see tools/prompts/make_pack.py).
rm -rf "${OUT:?}/prompts" && cp -r "$REPO/assets/prompts" "$OUT/prompts"
# portable.txt keeps settings, saves and caches in UserData next to the game.
touch "$OUT/portable.txt"
mkdir -p "$OUT/UserData/Music"
if [ ! -f "$OUT/UserData/Config.toml" ]; then
    DVD_ROOT="../Disc"
    [ "$DATA" = "$OUT/Disc" ] || DVD_ROOT="$DATA"
    printf '[paths]\ndvd_root = "%s"\n' "$DVD_ROOT" > "$OUT/UserData/Config.toml"
fi

printf '##progress 100\n'
cat <<EOF

Done. The game is "$OUT/ExciteTruck". The Excite PC launcher (scripts/build-launcher.sh) starts
it, with its settings and Music page. Your own songs go in $OUT/UserData/Music (MP3, up to 100).
In the game set Options > Race Music to All Random. F10 opens the settings bar.
EOF
