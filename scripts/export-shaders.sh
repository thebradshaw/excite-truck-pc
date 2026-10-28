#!/usr/bin/env bash
# Copy the shader recipes recorded while playing into assets/shaders/initial_pipeline_cache.db.
# The build ships that file next to the game, and every launch compiles all of them on the
# startup screen, so no shader is built for the first time in the middle of a race.
# Usage: scripts/export-shaders.sh [path/to/pipeline_cache.db]
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="${1:-$HOME/.local/share/WiiCompiled/Cache/pipeline_cache.db}"
DST=assets/shaders/initial_pipeline_cache.db
mkdir -p "$(dirname "$DST")"
python3 - "$SRC" "$DST" <<'PY'
import os, sqlite3, sys
src, dst = sys.argv[1:3]
tmp = dst + ".new"
if os.path.exists(tmp):
    os.remove(tmp)
con = sqlite3.connect(src)
# VACUUM INTO folds the WAL in and writes a compact standalone copy.
con.execute("VACUUM INTO ?", (tmp,))
con.close()
out = sqlite3.connect(tmp)
if os.path.exists(dst):
    # Keep recipes recorded earlier that this run did not see.
    out.execute("ATTACH ? AS old", (dst,))
    out.execute("INSERT OR IGNORE INTO pipeline_cache SELECT * FROM old.pipeline_cache")
    out.commit()
    out.execute("DETACH old")
out.execute("VACUUM")
rows = out.execute("SELECT type, config_version, COUNT(*) FROM pipeline_cache GROUP BY 1, 2").fetchall()
out.close()
os.replace(tmp, dst)
print(f"{dst}: " + ", ".join(f"type {t} v{v}: {n}" for t, v, n in rows))
PY
