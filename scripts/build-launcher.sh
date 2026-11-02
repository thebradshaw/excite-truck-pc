#!/usr/bin/env bash
# Builds the Excite PC launcher on Linux and installs it to <repo>/ExcitePC. Start it with
# ExcitePC/ExcitePC: it sets the games up from your discs (running scripts/build-linux.sh for
# you, with a progress bar), shows their settings and starts them. The games go beside it, in
# <repo>/ExciteTruck and <repo>/Excitebots.
#
# Needs: cmake, ninja, a C++20 compiler, git (for SDL3 and imgui; or set FETCHCONTENT_SOURCE_DIR_*
# in ET_CMAKE_ARGS), and the WiiCompiled submodule for toml11 (git submodule update --init).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$REPO/ExcitePC}"
BUILD="$REPO/build/launcher"

# shellcheck disable=SC2086
cmake -S "$REPO/launcher" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release ${ET_CMAKE_ARGS:-}
cmake --build "$BUILD"
mkdir -p "$OUT/setup/truck"
install -m 755 "$BUILD/ExcitePC" "$OUT/ExcitePC"
# The launcher looks for each game's build script in setup/<game>/.
cat > "$OUT/setup/truck/build-linux.sh" <<SH
#!/usr/bin/env bash
exec "$REPO/scripts/build-linux.sh" "\$@"
SH
chmod +x "$OUT/setup/truck/build-linux.sh"
if [ -f "$REPO/scripts/excitebots/build-linux.sh" ]; then
    mkdir -p "$OUT/setup/bots"
    cat > "$OUT/setup/bots/build-linux.sh" <<SH
#!/usr/bin/env bash
exec "$REPO/scripts/excitebots/build-linux.sh" "\$@"
SH
    chmod +x "$OUT/setup/bots/build-linux.sh"
fi
echo "Done. Start the launcher with: $OUT/ExcitePC"
